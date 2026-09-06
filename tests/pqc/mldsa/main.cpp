// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// mldsa -- ML-DSA-65 software baseline on Vortex.
//
// Deterministic keypair, signature and verification on the device, checked by
// the scheme's own invariant: the signature produced must verify. Known-answer
// vectors follow, as they did for ML-KEM; this first establishes that the
// library runs at all under a custom allocator, which ML-DSA needs and ML-KEM
// did not.
//
// Also reports the arena high-water mark, which is the working-set figure a
// hardware design has to budget for.

#include <vortex2.h>
#include "common.h"
#include "pqc_config.h"
#include "pqc_stack.h"
#include "mld_vortex_alloc.h"

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

#define STEP(msg) do { std::printf("[mldsa] %s\n", (msg)); std::fflush(stdout); } while (0)

int main(int argc, char** argv) {
    const char* kernel_file = "kernel.vxbin";
    uint32_t requests = 1, lanes = 1;
    int c;
    while ((c = getopt(argc, argv, "k:b:t:h")) != -1) {
        if (c == 'k') kernel_file = optarg;
        else if (c == 'b') requests = (uint32_t)std::atoi(optarg);
        else if (c == 't') lanes = (uint32_t)std::atoi(optarg);
        else { std::cout << "Usage: [-k kernel] [-b requests] [-t lanes] [-h]" << std::endl; std::exit(c == 'h' ? 0 : -1); }
    }
    if (requests < 1 || lanes < 1) { std::fprintf(stderr, "FAIL: -b and -t must be >= 1\n"); return -1; }
#if !defined(PQC_SIMT_KECCAK)
    if (lanes > 1) {
        std::fprintf(stderr, "FAIL: -t %u without SIMT_KECCAK=1 is pure redundancy --\n"
                     "      the extra lanes recompute the same chain and measure nothing.\n", lanes);
        return -1;
    }
#endif

    std::cout << "ML-DSA-" << MLD_CONFIG_PARAMETER_SET
              << "  pk=" << MLDSA_PK_BYTES
              << " sk=" << MLDSA_SK_BYTES
              << " sig=" << MLDSA_SIG_BYTES << std::endl;

    STEP("device_open");
    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    const pqc::config cfg = pqc::print_config(dev, requests, lanes);
    if (pqc::require_slots(cfg, requests, lanes) != 0) { vx_device_release(dev); return -1; }
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    STEP("buffer_create");
    struct { vx_buffer_h h; uint64_t bytes; int write; } bufs[] = {
        { nullptr, MLDSA_SEEDBYTES,                    0 },  // seed
        { nullptr, MLDSA_RNDBYTES,                     0 },  // rnd
        { nullptr, MLDSA_MSG_BYTES,                    0 },  // msg
        { nullptr, (uint64_t)requests * MLDSA_PK_BYTES,                    1 },
        { nullptr, (uint64_t)requests * MLDSA_SK_BYTES,                    1 },
        { nullptr, (uint64_t)requests * MLDSA_SIG_BYTES,                   1 },
        { nullptr, (uint64_t)requests * MLDSA_ST_COUNT * sizeof(int32_t),  1 },
        { nullptr, (uint64_t)requests * MLDSA_CY_COUNT * sizeof(uint64_t), 1 },
        { nullptr, (uint64_t)requests * MLDSA_AR_COUNT * sizeof(uint32_t), 1 },
    };
    for (auto& b : bufs)
        CHECK(vx_buffer_create(dev, b.bytes, b.write ? VX_MEM_WRITE : VX_MEM_READ, &b.h));

    kernel_arg_t arg{};
    uint64_t* slots[] = { &arg.seed_addr, &arg.rnd_addr, &arg.msg_addr,
                          &arg.pk_addr, &arg.sk_addr, &arg.sig_addr,
                          &arg.status_addr, &arg.cycles_addr, &arg.arena_addr };
    for (size_t i = 0; i < sizeof(bufs)/sizeof(bufs[0]); ++i)
        CHECK(vx_buffer_address(bufs[i].h, slots[i]));
    arg.requests = requests; arg.lanes = lanes;

    STEP("module_load");
    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    STEP("upload inputs");
    std::vector<uint8_t> h_seed(MLDSA_SEEDBYTES), h_rnd(MLDSA_RNDBYTES), h_msg(MLDSA_MSG_BYTES);
    for (size_t i = 0; i < h_seed.size(); ++i) h_seed[i] = static_cast<uint8_t>(i + 1);
    for (size_t i = 0; i < h_rnd.size();  ++i) h_rnd[i]  = static_cast<uint8_t>(0x40 + i);
    for (size_t i = 0; i < h_msg.size();  ++i) h_msg[i]  = static_cast<uint8_t>(0x80 + i);
    CHECK(vx_enqueue_write(q, bufs[0].h, 0, h_seed.data(), h_seed.size(), 0, nullptr, nullptr));
    CHECK(vx_enqueue_write(q, bufs[1].h, 0, h_rnd.data(),  h_rnd.size(),  0, nullptr, nullptr));
    CHECK(vx_enqueue_write(q, bufs[2].h, 0, h_msg.data(),  h_msg.size(),  0, nullptr, nullptr));

    std::vector<int32_t> h_st(requests * MLDSA_ST_COUNT, -12345);
    CHECK(vx_enqueue_write(q, bufs[6].h, 0, h_st.data(), h_st.size()*sizeof(int32_t), 0, nullptr, nullptr));

    STEP("launch");
    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = requests; li.block_dim[0] = lanes;
    vx_event_h lev = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    STEP("readback");
    std::vector<uint64_t> h_cy(requests * MLDSA_CY_COUNT, 0);
    std::vector<uint32_t> h_ar(requests * MLDSA_AR_COUNT, 0);
    std::vector<uint8_t>  h_pk(requests * MLDSA_PK_BYTES), h_sk(requests * MLDSA_SK_BYTES),
                          h_sig(requests * MLDSA_SIG_BYTES);
    vx_event_h e1=nullptr, e2=nullptr, e3=nullptr;
    CHECK(vx_enqueue_read(q, h_st.data(), bufs[6].h, 0, h_st.size()*sizeof(int32_t), 1, &lev, &e1));
    CHECK(vx_enqueue_read(q, h_cy.data(), bufs[7].h, 0, h_cy.size()*sizeof(uint64_t), 1, &lev, &e2));
    CHECK(vx_enqueue_read(q, h_ar.data(), bufs[8].h, 0, h_ar.size()*sizeof(uint32_t), 1, &lev, &e3));
    vx_event_h e4=nullptr, e5=nullptr, e6=nullptr;
    CHECK(vx_enqueue_read(q, h_pk.data(),  bufs[3].h, 0, h_pk.size(),  1, &lev, &e4));
    CHECK(vx_enqueue_read(q, h_sk.data(),  bufs[4].h, 0, h_sk.size(),  1, &lev, &e5));
    CHECK(vx_enqueue_read(q, h_sig.data(), bufs[5].h, 0, h_sig.size(), 1, &lev, &e6));
    STEP("wait");
    CHECK(vx_event_wait_value(e4, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e5, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e6, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e1, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e2, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e3, 1, VX_TIMEOUT_INFINITE));

    STEP("verify");
    int errors = 0;
    static const char* step_name[MLDSA_ST_COUNT] = { "keypair_internal", "signature_internal", "verify_internal" };

    // There is no published vector to compare against here -- this test drives
    // the internal, derandomised entry points with its own fixed seed. The
    // cryptographic anchor is instead verify_internal returning success: the
    // signature checks against the public key that keygen produced. The
    // cross-request anchor is byte-identical output, which holds because the
    // inputs are shared and signing is derandomised, and which is what a
    // request straying into a neighbour's workspace would break.
    for (uint32_t r = 0; r < requests; ++r) {
        const int32_t* st = &h_st[r * MLDSA_ST_COUNT];
        int req_err = 0;
        for (int i = 0; i < MLDSA_ST_COUNT; ++i)
            if (st[i] != 0) {
                std::printf("*** request %u: %s returned %d%s\n", r, step_name[i], st[i],
                            st[i] == -12345 ? "  (kernel never ran)" : "");
                ++req_err;
            }
        if (req_err == 0 && r > 0) {
            struct { const char* name; const uint8_t* p; size_t n; } out[] = {
                { "pk",  h_pk.data(),  MLDSA_PK_BYTES  },
                { "sk",  h_sk.data(),  MLDSA_SK_BYTES  },
                { "sig", h_sig.data(), MLDSA_SIG_BYTES },
            };
            for (auto& o : out)
                if (std::memcmp(o.p + r * o.n, o.p, o.n) != 0) {
                    size_t k = 0;
                    while (k < o.n && o.p[r * o.n + k] == o.p[k]) ++k;
                    std::printf("*** request %u: %s differs from request 0 at byte %zu"
                                " (got %02x, want %02x)\n", r, o.name, k,
                                o.p[r * o.n + k], o.p[k]);
                    ++req_err;
                }
        }
        errors += req_err;
    }

    uint32_t stack_max = 0, arena_max = 0, arena_fail = 0, span = 0;
    for (uint32_t r = 0; r < requests; ++r) {
        const uint32_t* ar = &h_ar[r * MLDSA_AR_COUNT];
        if (ar[MLDSA_AR_FAIL] != 0) {
            std::printf("*** request %u: arena exhausted %u times -- raise MLD_ARENA_BYTES\n",
                        r, ar[MLDSA_AR_FAIL]);
            ++errors;
        }
        // A peak of zero means MLD_CUSTOM_ALLOC never fired, so the library
        // allocated somewhere this build cannot see and the run describes a
        // different program. That happened once already, when the library and
        // the kernel were separate translation units and each got its own copy
        // of the file-scope arena.
        if (ar[MLDSA_AR_PEAK] == 0) {
            std::printf("*** request %u: arena peak is zero -- the custom allocator was"
                        " never reached\n", r);
            ++errors;
        }
        // The simulator cannot catch a stack overflow: RAM has no ACL and pages
        // in on demand, so a hart running past its slab just scribbles on a
        // neighbour. See tests/pqc/pqc_stack.h.
        if (ar[MLDSA_AR_STACK_PEAK] == 0) {
            std::printf("*** request %u: stack watermark is zero -- the probe never ran\n", r);
            ++errors;
        } else if (ar[MLDSA_AR_STACK_PEAK] >= ar[MLDSA_AR_STACK_SPAN]) {
            std::printf("*** request %u: stack peak %u B filled its whole %u B paintable"
                        " slab -- it overflowed into the next hart\n",
                        r, ar[MLDSA_AR_STACK_PEAK], ar[MLDSA_AR_STACK_SPAN]);
            ++errors;
        }
        if (ar[MLDSA_AR_STACK_PEAK] > stack_max) stack_max = ar[MLDSA_AR_STACK_PEAK];
        if (ar[MLDSA_AR_PEAK] > arena_max) arena_max = ar[MLDSA_AR_PEAK];
        arena_fail += ar[MLDSA_AR_FAIL];
        span = ar[MLDSA_AR_STACK_SPAN];
    }
    std::printf("STACK: peak=%u of %u paintable (slab %u)   ARENA: peak=%u of %u fail=%u"
                "   (max over %u requests)\n",
                stack_max, span, (unsigned)PQC_SLAB_BYTES,
                arena_max, (unsigned)MLD_ARENA_BYTES, arena_fail, requests);

    // Concurrent, not additive: the spans overlap, so the batch cost is the
    // device cycle count from --perf, not their sum. The spread is the read on
    // how much the requests interfered with each other.
    uint64_t tmin = ~0ull, tmax = 0, tsum = 0;
    for (uint32_t r = 0; r < requests; ++r) {
        const uint64_t* cy = &h_cy[r * MLDSA_CY_COUNT];
        for (int i = 0; i < MLDSA_CY_COUNT; ++i)
            if (cy[i] == 0) {
                std::printf("*** request %u: phase %d measured zero cycles\n", r, i);
                ++errors;
            }
        const uint64_t t = cy[0] + cy[1] + cy[2];
        if (t < tmin) tmin = t;
        if (t > tmax) tmax = t;
        tsum += t;
    }
    std::printf("CYCLES: keypair=%llu sign=%llu verify=%llu total=%llu   (request 0)\n",
                (unsigned long long)h_cy[0], (unsigned long long)h_cy[1],
                (unsigned long long)h_cy[2],
                (unsigned long long)(h_cy[0] + h_cy[1] + h_cy[2]));
    if (requests > 1)
        std::printf("REQUESTS: n=%u  per-request total min=%llu mean=%llu max=%llu"
                    "  spread=%.1f%%\n", requests,
                    (unsigned long long)tmin, (unsigned long long)(tsum / requests),
                    (unsigned long long)tmax,
                    100.0 * (double)(tmax - tmin) / (double)tmin);

    vx_event_release(e1); vx_event_release(e2); vx_event_release(e3);
    vx_event_release(e4); vx_event_release(e5); vx_event_release(e6);
    // Device-level counters. The per-request spans above overlap, so this is
    // the only figure that is the batch's actual cost, and IPC is the column
    // that says whether a batch stopped scaling on issue or on memory.
    vx_device_dump_perf(dev, stdout);

    vx_event_release(lev);
    for (auto& b : bufs) vx_buffer_release(b.h);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
