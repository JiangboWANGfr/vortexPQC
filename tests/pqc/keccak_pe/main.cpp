// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_config.h"

// The host reference is the library's own permutation, compiled for the host.
// SRCS and VX_SRCS are separate binaries, so including the .c here does not
// collide with the kernel's copy; tests/pqc/keccak_sg5 uses the same pattern.
extern "C" {
#include "src/common.h"
#include "src/fips202/keccakf1600.c"
}

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

int main(int argc, char** argv) {
    const char* kf = "kernel.vxbin";
    uint32_t blocks = 4, threads = 1, perms = 4;
    int c;
    while ((c = getopt(argc, argv, "k:b:t:p:h")) != -1) {
        if      (c == 'k') kf      = optarg;
        else if (c == 'b') blocks  = (uint32_t)std::atoi(optarg);
        else if (c == 't') threads = (uint32_t)std::atoi(optarg);
        else if (c == 'p') perms   = (uint32_t)std::atoi(optarg);
        else { std::printf("Usage: [-k kernel] [-b blocks] [-t threads] [-p perms]\n");
               std::exit(c == 'h' ? 0 : -1); }
    }
    if (blocks < 1 || threads < 1 || perms < 1) {
        std::printf("*** -b, -t and -p must all be >= 1\n"); return -1;
    }
    const uint32_t nharts = blocks * threads;

    // Validate the host reference before trusting it as the oracle: Keccak-f1600
    // on an all-zero state gives A[0][0] = f1258f7940e1dde7.
    {
        uint64_t z[KP_WORDS]; std::memset(z, 0, sizeof(z));
        mlk_keccakf1600_permute(z);
        if (z[0] != 0xf1258f7940e1dde7ULL) {
            std::printf("*** host reference fails the FIPS 202 all-zero KAT\n");
            return -1;
        }
    }

    vx_device_h dev = nullptr; CHECK(vx_device_open(0, &dev));
    const pqc::config cfg = pqc::print_config(dev, blocks, threads);
    if (pqc::require_slots(cfg, blocks, threads) != 0) { vx_device_release(dev); return -1; }
    vx_queue_info_t qi{sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0};
    vx_queue_h q = nullptr; CHECK(vx_queue_create(dev, &qi, &q));

    const size_t sbytes = (size_t)nharts * KP_WORDS * sizeof(uint64_t);
    vx_buffer_h sb = nullptr, cb = nullptr;
    CHECK(vx_buffer_create(dev, sbytes, VX_MEM_WRITE, &sb));
    CHECK(vx_buffer_create(dev, KP_CY_COUNT * sizeof(uint64_t), VX_MEM_WRITE, &cb));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(sb, &arg.states_addr));
    CHECK(vx_buffer_address(cb, &arg.cycles_addr));
    arg.perms = perms; arg.nharts = nharts; arg.threads = threads;

    // Poison: a kernel that never ran must fail, not read back zeros.
    std::vector<uint64_t> poison(nharts * KP_WORDS, 0xDEADBEEFDEADBEEFULL);
    CHECK(vx_enqueue_write(q, sb, 0, poison.data(), sbytes, 0, nullptr, nullptr));
    std::vector<uint64_t> cpoison(KP_CY_COUNT, 0xDEADBEEFDEADBEEFULL);
    CHECK(vx_enqueue_write(q, cb, 0, cpoison.data(), KP_CY_COUNT * 8, 0, nullptr, nullptr));

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kf, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    vx_launch_info_t li{}; li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = blocks; li.block_dim[0] = threads;
    vx_event_h lev = nullptr, e = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    std::vector<uint64_t> h_out(nharts * KP_WORDS), cyc(KP_CY_COUNT, 0);
    CHECK(vx_enqueue_read(q, h_out.data(), sb, 0, sbytes, 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e); e = nullptr;
    CHECK(vx_enqueue_read(q, cyc.data(), cb, 0, KP_CY_COUNT * 8, 1, &lev, &e));
    CHECK(vx_event_wait_value(e, 1, VX_TIMEOUT_INFINITE)); vx_event_release(e);

    int errors = 0;
    if (cyc[KP_CY_RUN] == 0xDEADBEEFDEADBEEFULL) {
        std::printf("*** cycle counter untouched -- the kernel never ran\n"); ++errors;
    }

    // Mirror the kernel loop word for word.
    uint32_t bad = 0;
    for (uint32_t h = 0; h < nharts; ++h) {
        uint64_t s[KP_WORDS];
        for (uint32_t j = 0; j < KP_WORDS; ++j) s[j] = kp_seed(h, j);
        for (uint32_t p = 0; p < perms; ++p) {
            for (uint32_t j = 0; j < KP_WORDS; ++j) s[j] ^= KP_XOR_K;
            mlk_keccakf1600_permute(s);
            uint64_t acc = 0;
            for (uint32_t j = 0; j < KP_WORDS; ++j) acc ^= s[j];
            s[0] ^= acc;
        }
        for (uint32_t j = 0; j < KP_WORDS; ++j) {
            const uint64_t got = h_out[(size_t)h * KP_WORDS + j];
            if (got != s[j]) {
                if (bad < 6) {
                    std::printf("    hart %u word %2u: got %016llx  want %016llx\n",
                                h, j, (unsigned long long)got, (unsigned long long)s[j]);
                }
                ++bad;
            }
        }
    }
    if (bad) {
        std::printf("*** %u of %u words wrong\n", bad, nharts * KP_WORDS);
        ++errors;
    }

    const double per = (double)cyc[KP_CY_RUN] / (double)perms;
    std::printf("KECCAK_PE blocks=%u threads=%u harts=%u perms=%u | hart0 cycles=%llu "
                "cy_per_perm=%.1f | bad=%u\n",
                blocks, threads, nharts, perms,
                (unsigned long long)cyc[KP_CY_RUN], per, bad);

    vx_device_dump_perf(dev, stdout);

    vx_buffer_release(sb); vx_buffer_release(cb);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);
    if (errors) { std::printf("FAILED!\n"); return 1; }
    std::printf("PASSED!\n");
    return 0;
}
