// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// profile -- exact primitive call counts for one ML-KEM-768 round trip.
//
// Combined with the per-call costs from tests/pqc/mlkem_microbench, this turns the
// end-to-end 33M cycles into an attribution: how much of it is Keccak, how much
// is the NTT, how much is sampling. That attribution is what decides which
// instruction to design first.

#include <vortex2.h>
#include "common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>

#define CHECK(expr) do { \
    vx_result_t _r = (expr); \
    if (_r != VX_SUCCESS) { \
        std::fprintf(stderr, "FAIL %s:%d: '%s' returned %s\n", \
                     __FILE__, __LINE__, #expr, vx_result_string(_r)); \
        std::exit(1); \
    } \
} while (0)

namespace {
const char* kernel_file = "kernel.vxbin";

// Per-call costs measured by tests/pqc/mlkem_microbench on the same configuration.
// Stated here so the report is self-contained; they are inputs to the
// attribution, not measurements this test makes.
struct Cost { const char* name; double cycles; };
const Cost kCost[MLK_PROF_COUNT] = {
    { "keccak_f1600_x1", 151777.0 },
    { "keccak_f1600_x4",      0.0 },  // not measured yet
    { "poly_ntt",        160220.0 },
    { "poly_invntt",     249571.0 },
    { "rej_uniform",          0.0 },  // counted below the SHAKE it consumes
    { "mulcache",             0.0 },
    { "basemul",              0.0 },
    { "poly_reduce",          0.0 },
};
} // namespace

int main(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "k:h")) != -1) {
        if (c == 'k') kernel_file = optarg;
        else { std::cout << "Usage: [-k kernel] [-h]" << std::endl; std::exit(c == 'h' ? 0 : -1); }
    }

    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    vx_buffer_h scr = nullptr, cnt = nullptr, st = nullptr, cyc = nullptr;
    CHECK(vx_buffer_create(dev, P_SCRATCH_LEN, VX_MEM_WRITE, &scr));
    CHECK(vx_buffer_create(dev, MLK_PROF_COUNT * sizeof(uint32_t), VX_MEM_WRITE, &cnt));
    CHECK(vx_buffer_create(dev, 3 * sizeof(int32_t), VX_MEM_WRITE, &st));
    CHECK(vx_buffer_create(dev, 3 * sizeof(uint64_t), VX_MEM_WRITE, &cyc));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(scr, &arg.scratch_addr));
    CHECK(vx_buffer_address(cnt, &arg.counts_addr));
    CHECK(vx_buffer_address(st,  &arg.status_addr));
    CHECK(vx_buffer_address(cyc, &arg.cycles_addr));

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    // Fixed coins, so the count is reproducible: rejection sampling retries
    // depend on the bytes it draws, and therefore so does the Keccak count.
    std::vector<uint8_t> h_scr(P_SCRATCH_LEN, 0);
    for (int i = 0; i < 96; ++i) h_scr[i] = static_cast<uint8_t>(i);
    CHECK(vx_enqueue_write(q, scr, 0, h_scr.data(), h_scr.size(), 0, nullptr, nullptr));

    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = 1; li.block_dim[0] = 1;
    vx_event_h lev = nullptr, e1 = nullptr, e2 = nullptr, e3 = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    std::vector<uint32_t> h_cnt(MLK_PROF_COUNT, 0);
    std::vector<int32_t>  h_st(3, -1);
    CHECK(vx_enqueue_read(q, h_cnt.data(), cnt, 0, h_cnt.size() * sizeof(uint32_t), 1, &lev, &e1));
    CHECK(vx_enqueue_read(q, h_st.data(),  st,  0, h_st.size()  * sizeof(int32_t),  1, &lev, &e2));
    CHECK(vx_event_wait_value(e1, 1, VX_TIMEOUT_INFINITE));
    std::vector<uint64_t> h_cyc(3, 0);
    CHECK(vx_enqueue_read(q, h_cyc.data(), cyc, 0, h_cyc.size() * sizeof(uint64_t), 1, &lev, &e3));
    CHECK(vx_event_wait_value(e2, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e3, 1, VX_TIMEOUT_INFINITE));

    int errors = 0;
#if defined(PQC_ABLATE_KECCAK) || defined(PQC_ABLATE_NTT)
    // An ablated build computes the wrong answer on purpose; only the cycle
    // counts mean anything, and a nonzero status here would be the library
    // rejecting its own malformed intermediate rather than a real fault.
    std::printf("ABLATION BUILD -- results are invalid by construction\n");
#else
    for (int i = 0; i < 3; ++i)
        if (h_st[i] != 0) { std::printf("*** KEM step %d returned %d\n", i, h_st[i]); ++errors; }
#endif

    std::printf("%-18s %10s %14s\n", "primitive", "calls", "est_cycles");
    double attributed = 0;
    for (int i = 0; i < MLK_PROF_COUNT; ++i) {
        const double est = h_cnt[i] * kCost[i].cycles;
        attributed += est;
        if (kCost[i].cycles > 0)
            std::printf("%-18s %10u %14.0f\n", kCost[i].name, h_cnt[i], est);
        else
            std::printf("%-18s %10u %14s\n", kCost[i].name, h_cnt[i], "-");
    }
    // A count of zero for a primitive ML-KEM demonstrably uses means the hook
    // was not wired in -- the library would have run its own code either way,
    // so the run passes while the profile describes nothing.
    static const int must_be_called[] = {
        MLK_PROF_KECCAK_X1, MLK_PROF_NTT, MLK_PROF_INTT, MLK_PROF_REJ_UNIFORM
    };
    for (int i : must_be_called)
        if (h_cnt[i] == 0) {
            std::printf("*** %s was never called -- its hook is not wired in\n",
                        kCost[i].name);
            ++errors;
        }

    std::printf("attributed cycles: %.0f\n", attributed);
    for (int i = 0; i < 3; ++i)
        if (h_cyc[i] == 0) { std::printf("*** phase %d measured zero cycles\n", i); ++errors; }
    const uint64_t total = h_cyc[0] + h_cyc[1] + h_cyc[2];
    std::printf("CYCLES: keypair=%llu encaps=%llu decaps=%llu total=%llu\n",
                (unsigned long long)h_cyc[0], (unsigned long long)h_cyc[1],
                (unsigned long long)h_cyc[2], (unsigned long long)total);

    vx_event_release(e1); vx_event_release(e2); vx_event_release(e3);
    vx_event_release(lev);
    vx_buffer_release(scr); vx_buffer_release(cnt); vx_buffer_release(st);
    vx_buffer_release(cyc);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
