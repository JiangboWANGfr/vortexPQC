#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "common.h"
#include "pqc_config.h"

#define CHECK(_expr) do { int _r = _expr; if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_expr, _r); \
  std::exit(-1); } } while (0)

int main(int argc, char** argv) {
    const char* kf = "kernel.vxbin";
    uint32_t lanes = 1, blocks = 1;
    int c;
    while ((c = getopt(argc, argv, "k:t:b:h")) != -1) {
        if (c == 'k') kf = optarg;
        else if (c == 't') lanes = (uint32_t)atoi(optarg);
        else if (c == 'b') blocks = (uint32_t)atoi(optarg);
        else { std::cout << "Usage: [-k kernel] [-t lanes]\n"; std::exit(c=='h'?0:-1); }
    }

    vx_device_h dev=nullptr; CHECK(vx_device_open(0,&dev));
    const pqc::config cfg = pqc::print_config(dev, blocks, lanes);
    if (pqc::require_slots(cfg, blocks, lanes) != 0) { vx_device_release(dev); return -1; }
    vx_queue_info_t qi={sizeof(qi),nullptr,VX_QUEUE_PRIORITY_NORMAL,0};
    vx_queue_h q=nullptr; CHECK(vx_queue_create(dev,&qi,&q));

    if (blocks < 1 || lanes < 1) { std::fprintf(stderr, "FAIL: -b and -t must be >= 1\n"); return -1; }

    // Sized by the launch, not by a fixed maximum: CTA b writes at
    // b * GX_ENTRIES * 256, so a buffer cut to a constant silently overflows
    // the moment -b exceeds it, and the corruption lands in whatever device
    // allocation follows.
    vx_buffer_h pb=nullptr,nb=nullptr,cb=nullptr,sb=nullptr,xb=nullptr;
    CHECK(vx_buffer_create(dev, (uint64_t)blocks*GX_ENTRIES*256*2, VX_MEM_WRITE, &pb));
    CHECK(vx_buffer_create(dev, (uint64_t)blocks*GX_NOISE*256*2, VX_MEM_WRITE, &nb));
    CHECK(vx_buffer_create(dev, 4*sizeof(uint64_t), VX_MEM_WRITE, &cb));
    CHECK(vx_buffer_create(dev, 2*sizeof(uint32_t), VX_MEM_WRITE, &sb));
    CHECK(vx_buffer_create(dev, GX_HARTS*sizeof(uint32_t), VX_MEM_WRITE, &xb));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(pb,&arg.poly_addr));
    CHECK(vx_buffer_address(nb,&arg.noise_addr));
    CHECK(vx_buffer_address(cb,&arg.cycles_addr));
    CHECK(vx_buffer_address(sb,&arg.sum_addr));
    CHECK(vx_buffer_address(xb,&arg.xof_addr));
    arg.lanes = lanes; arg.blocks = blocks;

    vx_module_h mod=nullptr; vx_kernel_h kern=nullptr;
    CHECK(vx_module_load_file(dev,kf,&mod));
    CHECK(vx_module_get_kernel(mod,"main",&kern));

    vx_launch_info_t li{}; li.struct_size=sizeof(li); li.kernel=kern;
    li.args_host=&arg; li.args_size=sizeof(arg);
    li.ndim=1; li.grid_dim[0]=blocks; li.block_dim[0]=lanes;
    // The kernel accumulates into xof[], so it has to start at zero.
    std::vector<uint32_t> zero(GX_HARTS, 0);
    vx_event_h zev=nullptr;
    CHECK(vx_enqueue_write(q, xb, 0, zero.data(), zero.size()*4, 0, nullptr, &zev));
    CHECK(vx_event_wait_value(zev,1,VX_TIMEOUT_INFINITE)); vx_event_release(zev);

    vx_event_h lev=nullptr,e=nullptr;
    CHECK(vx_enqueue_launch(q,&li,0,nullptr,&lev));

    std::vector<uint64_t> cyc(4,0); std::vector<uint32_t> sum(2,0);
    std::vector<uint32_t> xof(GX_HARTS,0);
    CHECK(vx_enqueue_read(q,cyc.data(),cb,0,cyc.size()*8,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,sum.data(),sb,0,sum.size()*4,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,xof.data(),xb,0,xof.size()*4,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e);

    uint64_t xof_total = 0;
    for (uint32_t v : xof) xof_total += v;

    int errors = 0;
    // CTA 0's output does not depend on the width or the message count, so a
    // mismatch means a wide or batched arm computed something different from
    // the scalar one. Printing it without comparing it -- which this test used
    // to do -- makes PASSED! mean nothing.
    if (sum[0] != GX_SUM_POLY || sum[1] != GX_SUM_NOISE) {
        std::printf("*** checksum mismatch: got %08x/%08x, expected %08x/%08x\n",
                    sum[0], sum[1], GX_SUM_POLY, GX_SUM_NOISE);
        ++errors;
    }
    if (xof_total == 0) { std::printf("*** XOF block counter read zero\n"); ++errors; }
    if (cyc[0] == 0 || cyc[1] == 0) { std::printf("*** a phase measured zero cycles\n"); ++errors; }
    if (cyc[3] == 0) { std::printf("*** stack watermark is zero -- the probe never ran\n"); ++errors; }
    else if (cyc[3] >= cyc[2]) {
        std::printf("*** stack peak %llu B filled its whole %llu B paintable slab"
                    " -- it overflowed into the next hart\n",
                    (unsigned long long)cyc[3], (unsigned long long)cyc[2]);
        ++errors;
    }

    std::printf("GX B=%u W=%u genmat=%llu noise=%llu xof_blocks=%llu "
                "stack=%llu/%llu sum=%08x/%08x\n",
                blocks,lanes,(unsigned long long)cyc[0],(unsigned long long)cyc[1],
                (unsigned long long)xof_total,
                (unsigned long long)cyc[3],(unsigned long long)cyc[2],sum[0],sum[1]);

    vx_device_dump_perf(dev, stdout);

    vx_event_release(lev);
    vx_buffer_release(pb); vx_buffer_release(nb); vx_buffer_release(cb);
    vx_buffer_release(sb); vx_buffer_release(xb);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);
    std::cout << (errors ? "FAILED!" : "PASSED!") << std::endl;
    return errors ? -1 : 0;
}
