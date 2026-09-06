#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "common.h"

#define CHECK(_expr)                                                     \
  do {                                                                   \
    int _ret = _expr;                                                    \
    if (0 == _ret) break;                                                \
    std::fprintf(stderr, "FAIL %s:%d: '%s' returned %d\n",               \
                 __FILE__, __LINE__, #_expr, _ret);                      \
    std::exit(-1);                                                       \
  } while (false)

int main(int argc, char** argv) {
    const char* kernel_file = "kernel.vxbin";
    uint32_t lanes = 1, blocks = 1;
    int c;
    while ((c = getopt(argc, argv, "k:t:b:h")) != -1) {
        if (c == 'k') kernel_file = optarg;
        else if (c == 't') lanes = (uint32_t)atoi(optarg);
        else if (c == 'b') blocks = (uint32_t)atoi(optarg);
        else { std::cout << "Usage: [-k kernel] [-t lanes]" << std::endl; std::exit(c == 'h' ? 0 : -1); }
    }

    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    vx_buffer_h scr = nullptr, cnt = nullptr, st = nullptr, cyc = nullptr, stk = nullptr;
    CHECK(vx_buffer_create(dev, P_SCRATCH_LEN, VX_MEM_WRITE, &scr));
    CHECK(vx_buffer_create(dev, MLKW_COUNT * sizeof(uint32_t), VX_MEM_WRITE, &cnt));
    CHECK(vx_buffer_create(dev, 3 * sizeof(int32_t), VX_MEM_WRITE, &st));
    CHECK(vx_buffer_create(dev, 3 * sizeof(uint64_t), VX_MEM_WRITE, &cyc));
    CHECK(vx_buffer_create(dev, 4 * sizeof(uint32_t), VX_MEM_WRITE, &stk));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(scr, &arg.scratch_addr));
    CHECK(vx_buffer_address(cnt, &arg.counts_addr));
    CHECK(vx_buffer_address(st,  &arg.status_addr));
    CHECK(vx_buffer_address(cyc, &arg.cycles_addr));
    CHECK(vx_buffer_address(stk, &arg.stack_addr));
    arg.lanes = lanes;

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    // Same fixed coins as the profile test, so the Keccak counts are the same
    // counts and the arms are comparable byte for byte.
    std::vector<uint8_t> h_scr(P_SCRATCH_LEN, 0);
    for (int i = 0; i < 96; ++i) h_scr[i] = static_cast<uint8_t>(i);
    CHECK(vx_enqueue_write(q, scr, 0, h_scr.data(), h_scr.size(), 0, nullptr, nullptr));

    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = blocks; li.block_dim[0] = lanes;
    vx_event_h lev = nullptr, e = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    std::vector<uint32_t> h_cnt(MLKW_COUNT, 0);
    std::vector<int32_t>  h_st(3, -1);
    std::vector<uint64_t> h_cyc(3, 0);
    std::vector<uint32_t> h_stk(4, 0);
    CHECK(vx_enqueue_read(q, h_cnt.data(), cnt, 0, h_cnt.size()*sizeof(uint32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_st.data(), st, 0, h_st.size()*sizeof(int32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_cyc.data(), cyc, 0, h_cyc.size()*sizeof(uint64_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_stk.data(), stk, 0, h_stk.size()*sizeof(uint32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;

    int errors = 0;
    for (int i = 0; i < 3; ++i)
        if (h_st[i] != 0) { std::printf("*** KEM step %d returned %d\n", i, h_st[i]); ++errors; }

    const uint64_t total = h_cyc[0] + h_cyc[1] + h_cyc[2];
    std::printf("BLOCKS %u WIDTH %u  keccak_x1=%u keccak_x4=%u slots=%u  stack_peak=%u/%u  arena_peak=%u fail=%u\n",
                blocks, lanes, h_cnt[MLKW_KECCAK_X1], h_cnt[MLKW_KECCAK_X4],
                h_cnt[MLKW_SLOTS], h_stk[0], h_stk[1], h_stk[2], h_stk[3]);
    std::printf("PHASE x1: kp=%u enc=%u dec=%u   x4: kp=%u enc=%u dec=%u\n",
                h_cnt[MLKW_X1_KP], h_cnt[MLKW_X1_ENC]-h_cnt[MLKW_X1_KP],
                h_cnt[MLKW_X1_DEC]-h_cnt[MLKW_X1_ENC],
                h_cnt[MLKW_X4_KP], h_cnt[MLKW_X4_ENC]-h_cnt[MLKW_X4_KP],
                h_cnt[MLKW_X4_DEC]-h_cnt[MLKW_X4_ENC]);
    std::printf("CYCLES: keypair=%llu encaps=%llu decaps=%llu total=%llu\n",
                (unsigned long long)h_cyc[0], (unsigned long long)h_cyc[1],
                (unsigned long long)h_cyc[2], (unsigned long long)total);

    vx_device_dump_perf(dev, stdout);
    vx_event_release(lev);
    vx_buffer_release(scr); vx_buffer_release(cnt); vx_buffer_release(st);
    vx_buffer_release(cyc); vx_buffer_release(stk);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
