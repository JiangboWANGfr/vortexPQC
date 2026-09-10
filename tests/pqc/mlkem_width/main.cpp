#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "common.h"
#include "pqc_config.h"

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
#if defined(PQC_KECCAK_SG25)
    uint32_t lanes = 32, blocks = 1;
#else
    uint32_t lanes = 1, blocks = 1;
#endif
    int c;
    while ((c = getopt(argc, argv, "k:t:b:h")) != -1) {
        if (c == 'k') kernel_file = optarg;
        else if (c == 't') lanes = (uint32_t)atoi(optarg);
        else if (c == 'b') blocks = (uint32_t)atoi(optarg);
        else { std::cout << "Usage: [-k kernel] [-t lanes]" << std::endl; std::exit(c == 'h' ? 0 : -1); }
    }

#if defined(PQC_KECCAK_SG25)
    if (lanes != 32) {
        std::fprintf(stderr, "FAIL: SG25 requires exactly 32 lanes\n");
        return 1;
    }
#endif

    // Each CTA owns one scratch slice. SG25 treats its lanes as one subgroup;
    // the other backends use the requested lane-width mapping.
    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    const pqc::config cfg = pqc::print_config(dev, blocks, lanes);
    if (pqc::require_slots(cfg, blocks, lanes) != 0) { vx_device_release(dev); return -1; }
#if defined(PQC_KECCAK_KROUND25)
    if ((cfg.isa_flags & VX_ISA_EXT_KROUND25) == 0) {
        std::fprintf(stderr, "KROUND SG25 requires a device with KROUND25 enabled\n");
        vx_device_release(dev);
        return 1;
    }
#endif
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    vx_buffer_h scr = nullptr, cnt = nullptr, st = nullptr, cyc = nullptr, stk = nullptr;
    if (blocks > MLKW_MAX_WARPS) {
        std::fprintf(stderr, "FAIL: -b %u exceeds MLKW_MAX_WARPS (%u): the per-warp\n"
                     "      counter rows would alias.\n", blocks, (unsigned)MLKW_MAX_WARPS);
        return -1;
    }
    CHECK(vx_buffer_create(dev, (uint64_t)blocks * P_SCRATCH_LEN, VX_MEM_WRITE, &scr));
    CHECK(vx_buffer_create(dev, (uint64_t)blocks * MLKW_COUNT * sizeof(uint32_t), VX_MEM_WRITE, &cnt));
    CHECK(vx_buffer_create(dev, (uint64_t)blocks * 3 * sizeof(int32_t), VX_MEM_WRITE, &st));
    CHECK(vx_buffer_create(dev, (uint64_t)blocks * 3 * sizeof(uint64_t), VX_MEM_WRITE, &cyc));
    CHECK(vx_buffer_create(dev, (uint64_t)blocks * 4 * sizeof(uint32_t), VX_MEM_WRITE, &stk));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(scr, &arg.scratch_addr));
    CHECK(vx_buffer_address(cnt, &arg.counts_addr));
    CHECK(vx_buffer_address(st,  &arg.status_addr));
    CHECK(vx_buffer_address(cyc, &arg.cycles_addr));
    CHECK(vx_buffer_address(stk, &arg.stack_addr));
    arg.lanes = lanes; arg.requests = blocks;

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    // Same fixed coins as the profile test, so the Keccak counts are the same
    // counts and the arms are comparable byte for byte.
    std::vector<uint8_t> h_scr((size_t)blocks * P_SCRATCH_LEN, 0);
    for (uint32_t b = 0; b < blocks; ++b)
        for (int i = 0; i < 96; ++i) h_scr[(size_t)b * P_SCRATCH_LEN + i] = static_cast<uint8_t>(i);
    CHECK(vx_enqueue_write(q, scr, 0, h_scr.data(), h_scr.size(), 0, nullptr, nullptr));

    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = blocks; li.block_dim[0] = lanes;
    vx_event_h lev = nullptr, e = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    std::vector<uint32_t> h_cnt((size_t)blocks * MLKW_COUNT, 0);
    std::vector<int32_t>  h_st((size_t)blocks * 3, -1);
    std::vector<uint64_t> h_cyc((size_t)blocks * 3, 0);
    std::vector<uint32_t> h_stk((size_t)blocks * 4, 0);
    CHECK(vx_enqueue_read(q, h_cnt.data(), cnt, 0, h_cnt.size()*sizeof(uint32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_st.data(), st, 0, h_st.size()*sizeof(int32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_cyc.data(), cyc, 0, h_cyc.size()*sizeof(uint64_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, h_stk.data(), stk, 0, h_stk.size()*sizeof(uint32_t), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;

    // Read the whole scratch back: the return codes say the library did not
    // error, not that it computed the right thing. Checking only them is how a
    // wide arm that silently corrupts a shared Keccak state still reports PASSED.
    std::vector<uint8_t> h_out((size_t)blocks * P_SCRATCH_LEN, 0);
    CHECK(vx_enqueue_read(q, h_out.data(), scr, 0, h_out.size(), 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;

    int errors = 0;
    // Every request runs the same coins, so every request must land on the same
    // key material. Checking only request 0 is how a batch that corrupted its
    // neighbours would still report a clean measurement.
    auto fnv = [](const uint8_t* p, size_t n) {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
        return h;
    };
    for (uint32_t b = 0; b < blocks; ++b) {
        const uint8_t* o = &h_out[(size_t)b * P_SCRATCH_LEN];
        const int32_t* st_b = &h_st[(size_t)b * 3];
        for (int i = 0; i < 3; ++i)
            if (st_b[i] != 0) {
                std::printf("*** request %u: KEM step %d returned %d%s\n", b, i, st_b[i],
                            st_b[i] == -1 ? "  (kernel never ran)" : "");
                ++errors;
            }
        if (std::memcmp(o + P_OFF_SS_ENC, o + P_OFF_SS_DEC, 32) != 0) {
            std::printf("*** request %u: shared secrets differ\n", b);
            ++errors;
        }
        struct { const char* n; uint32_t off, len, want; } chk[] = {
            { "pk", P_OFF_PK,   1184, MLKW_SUM_PK }, { "sk", P_OFF_SK,   2400, MLKW_SUM_SK },
            { "ct", P_OFF_CT,   1088, MLKW_SUM_CT }, { "ss", P_OFF_SS_ENC, 32, MLKW_SUM_SS },
        };
        for (auto& k : chk) {
            uint32_t got = fnv(o + k.off, k.len);
            if (got != k.want) {
                std::printf("*** request %u: %s checksum %08x, expected %08x\n",
                            b, k.n, got, k.want);
                ++errors;
            }
        }
    }
    uint64_t cmin = ~0ull, cmax = 0;
    for (uint32_t b = 0; b < blocks; ++b) {
        const uint64_t t = h_cyc[(size_t)b*3] + h_cyc[(size_t)b*3+1] + h_cyc[(size_t)b*3+2];
        if (t < cmin) cmin = t;
        if (t > cmax) cmax = t;
    }

    const uint64_t total = h_cyc[0] + h_cyc[1] + h_cyc[2];
#if defined(PQC_KECCAK_SG25)
    std::printf("BLOCKS %u WIDTH %u  sg25_permutations=%u slots=%u  stack_peak=%u/%u  arena_peak=%u fail=%u\n",
                blocks, lanes, h_cnt[MLKW_KECCAK_X1], h_cnt[MLKW_SLOTS],
                h_stk[0], h_stk[1], h_stk[2], h_stk[3]);
#else
    std::printf("BLOCKS %u WIDTH %u  keccak_x1=%u keccak_x4=%u slots=%u  stack_peak=%u/%u  arena_peak=%u fail=%u\n",
                blocks, lanes, h_cnt[MLKW_KECCAK_X1], h_cnt[MLKW_KECCAK_X4],
                h_cnt[MLKW_SLOTS], h_stk[0], h_stk[1], h_stk[2], h_stk[3]);
#endif
    if (blocks > 1)
        std::printf("REQUESTS: n=%u  per-request total min=%llu max=%llu  spread=%.1f%%\n",
                    blocks, (unsigned long long)cmin, (unsigned long long)cmax,
                    100.0 * (double)(cmax - cmin) / (double)cmin);
#if defined(PQC_KECCAK_SG25)
    std::printf("PHASE permutations: kp=%u enc=%u dec=%u\n",
                h_cnt[MLKW_X1_KP], h_cnt[MLKW_X1_ENC]-h_cnt[MLKW_X1_KP],
                h_cnt[MLKW_X1_DEC]-h_cnt[MLKW_X1_ENC]);
#else
    std::printf("PHASE x1: kp=%u enc=%u dec=%u   x4: kp=%u enc=%u dec=%u\n",
                h_cnt[MLKW_X1_KP], h_cnt[MLKW_X1_ENC]-h_cnt[MLKW_X1_KP],
                h_cnt[MLKW_X1_DEC]-h_cnt[MLKW_X1_ENC],
                h_cnt[MLKW_X4_KP], h_cnt[MLKW_X4_ENC]-h_cnt[MLKW_X4_KP],
                h_cnt[MLKW_X4_DEC]-h_cnt[MLKW_X4_ENC]);
#endif
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
