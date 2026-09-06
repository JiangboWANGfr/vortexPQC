#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_stack.h"

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

int main(int argc, char** argv) {
    const char* kf = "kernel.vxbin";
    uint32_t lanes = 1;
    int c;
    while ((c = getopt(argc, argv, "k:t:h")) != -1) {
        if (c == 'k') kf = optarg;
        else if (c == 't') lanes = (uint32_t)std::atoi(optarg);
        else { std::cout << "Usage: [-k kernel] [-t lanes]\n"; std::exit(c == 'h' ? 0 : -1); }
    }
    if (lanes < 1) { std::fprintf(stderr, "FAIL: -t must be >= 1\n"); return -1; }

    vx_device_h dev=nullptr; CHECK(vx_device_open(0,&dev));
    vx_queue_info_t qi={sizeof(qi),nullptr,VX_QUEUE_PRIORITY_NORMAL,0};
    vx_queue_h q=nullptr; CHECK(vx_queue_create(dev,&qi,&q));

    vx_buffer_h pb=nullptr, rb=nullptr, cb=nullptr, mb=nullptr;
    CHECK(vx_buffer_create(dev, NTT_N*sizeof(int32_t), VX_MEM_WRITE, &pb));
    CHECK(vx_buffer_create(dev, NTT_N*sizeof(int32_t), VX_MEM_WRITE, &rb));
    CHECK(vx_buffer_create(dev, NTT_CY_COUNT*sizeof(uint64_t), VX_MEM_WRITE, &cb));
    CHECK(vx_buffer_create(dev, sizeof(uint32_t), VX_MEM_WRITE, &mb));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(pb,&arg.poly_addr));
    CHECK(vx_buffer_address(rb,&arg.ref_addr));
    CHECK(vx_buffer_address(cb,&arg.cycles_addr));
    CHECK(vx_buffer_address(mb,&arg.mism_addr));
    arg.lanes = lanes;

    // Poison, so a kernel that never ran fails instead of matching two buffers
    // nobody wrote.
    std::vector<uint32_t> poison(1, 0xDEADBEEFu);
    CHECK(vx_enqueue_write(q, mb, 0, poison.data(), sizeof(uint32_t), 0, nullptr, nullptr));

    vx_module_h mod=nullptr; vx_kernel_h kern=nullptr;
    CHECK(vx_module_load_file(dev,kf,&mod));
    CHECK(vx_module_get_kernel(mod,"main",&kern));

    vx_launch_info_t li{}; li.struct_size=sizeof(li); li.kernel=kern;
    li.args_host=&arg; li.args_size=sizeof(arg);
    li.ndim=1; li.grid_dim[0]=1; li.block_dim[0]=lanes;
    vx_event_h lev=nullptr,e=nullptr;
    CHECK(vx_enqueue_launch(q,&li,0,nullptr,&lev));

    std::vector<int32_t> h_out(NTT_N), h_ref(NTT_N);
    std::vector<uint64_t> cyc(NTT_CY_COUNT,0);
    std::vector<uint32_t> mism(1,0);
    CHECK(vx_enqueue_read(q,h_out.data(),pb,0,h_out.size()*4,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,h_ref.data(),rb,0,h_ref.size()*4,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,cyc.data(),cb,0,cyc.size()*8,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,mism.data(),mb,0,sizeof(uint32_t),1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e);

    int errors = 0;
    if (mism[0] == 0xDEADBEEFu) {
        std::printf("*** mismatch counter untouched -- the kernel never ran\n"); ++errors;
    } else if (mism[0] != 0) {
        std::printf("*** %u of %d coefficients differ from the library's own NTT\n",
                    mism[0], NTT_N);
        for (int i = 0, shown = 0; i < NTT_N && shown < 4; ++i)
            if (h_out[i] != h_ref[i]) {
                std::printf("    [%3d] coop %6d  ref %6d\n", i, h_out[i], h_ref[i]);
                ++shown;
            }
        ++errors;
    }
    if (cyc[NTT_CY_COOP] == 0 || cyc[NTT_CY_REF] == 0) {
        std::printf("*** a phase measured zero cycles\n"); ++errors;
    }
    if (cyc[NTT_CY_STACK] >= cyc[NTT_CY_SPAN]) {
        std::printf("*** stack peak %llu B filled its whole %llu B paintable slab\n",
                    (unsigned long long)cyc[NTT_CY_STACK],
                    (unsigned long long)cyc[NTT_CY_SPAN]); ++errors;
    }

    std::printf("NTT W=%u  coop=%llu  ref=%llu  speedup=%.3f  stack=%llu/%llu  mismatch=%u\n",
                lanes, (unsigned long long)cyc[NTT_CY_COOP],
                (unsigned long long)cyc[NTT_CY_REF],
                cyc[NTT_CY_COOP] ? (double)cyc[NTT_CY_REF]/(double)cyc[NTT_CY_COOP] : 0.0,
                (unsigned long long)cyc[NTT_CY_STACK], (unsigned long long)cyc[NTT_CY_SPAN],
                mism[0]);
    vx_device_dump_perf(dev, stdout);

    vx_event_release(lev);
    vx_buffer_release(pb); vx_buffer_release(rb); vx_buffer_release(cb); vx_buffer_release(mb);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);
    std::cout << (errors ? "FAILED!" : "PASSED!") << std::endl;
    return errors ? -1 : 0;
}
