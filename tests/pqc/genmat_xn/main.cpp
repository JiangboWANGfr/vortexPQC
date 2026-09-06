#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "common.h"

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
    vx_queue_info_t qi={sizeof(qi),nullptr,VX_QUEUE_PRIORITY_NORMAL,0};
    vx_queue_h q=nullptr; CHECK(vx_queue_create(dev,&qi,&q));

    vx_buffer_h pb=nullptr,nb=nullptr,cb=nullptr,sb=nullptr;
    CHECK(vx_buffer_create(dev, GX_MAXBLK*GX_ENTRIES*256*2, VX_MEM_WRITE, &pb));
    CHECK(vx_buffer_create(dev, GX_MAXBLK*GX_NOISE*256*2, VX_MEM_WRITE, &nb));
    CHECK(vx_buffer_create(dev, 4*sizeof(uint64_t), VX_MEM_WRITE, &cb));
    CHECK(vx_buffer_create(dev, 2*sizeof(uint32_t), VX_MEM_WRITE, &sb));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(pb,&arg.poly_addr));
    CHECK(vx_buffer_address(nb,&arg.noise_addr));
    CHECK(vx_buffer_address(cb,&arg.cycles_addr));
    CHECK(vx_buffer_address(sb,&arg.sum_addr));
    arg.lanes = lanes; arg.blocks = blocks;

    vx_module_h mod=nullptr; vx_kernel_h kern=nullptr;
    CHECK(vx_module_load_file(dev,kf,&mod));
    CHECK(vx_module_get_kernel(mod,"main",&kern));

    vx_launch_info_t li{}; li.struct_size=sizeof(li); li.kernel=kern;
    li.args_host=&arg; li.args_size=sizeof(arg);
    li.ndim=1; li.grid_dim[0]=blocks; li.block_dim[0]=lanes;
    vx_event_h lev=nullptr,e=nullptr;
    CHECK(vx_enqueue_launch(q,&li,0,nullptr,&lev));

    std::vector<uint64_t> cyc(4,0); std::vector<uint32_t> sum(2,0);
    CHECK(vx_enqueue_read(q,cyc.data(),cb,0,cyc.size()*8,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,sum.data(),sb,0,sum.size()*4,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e);

    std::printf("GX B=%u W=%u genmat=%llu noise=%llu blocks=%llu stack=%llu sum=%08x/%08x\n",
                blocks,lanes,(unsigned long long)cyc[0],(unsigned long long)cyc[1],
                (unsigned long long)cyc[2],(unsigned long long)cyc[3],sum[0],sum[1]);

    vx_device_dump_perf(dev, stdout);

    vx_event_release(lev);
    vx_buffer_release(pb); vx_buffer_release(nb); vx_buffer_release(cb); vx_buffer_release(sb);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);
    std::cout << "PASSED!" << std::endl;
    return 0;
}
