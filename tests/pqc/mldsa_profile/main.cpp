// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// mldsa_profile -- exact primitive call counts for one ML-DSA-65 round trip,
// and the ablation that measures each primitive's share directly.
//
// Same three operations as the mldsa baseline, with the counting backends
// attached. Build with ABLATE=keccak or ABLATE=ntt to turn those hooks into
// no-ops: the cycle delta against ABLATE unset is that primitive's measured
// cost, which is a stronger claim than a call count times a microbenchmark.

#include <vortex2.h>
#include "common.h"
#include "pqc_config.h"
#include "pqc_stack.h"
#include "mld_vortex_alloc.h"
#include "host_reference.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
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

static uint32_t parse_u32(const char* value) {
    char* end = nullptr;
    errno = 0;
    unsigned long parsed = std::strtoul(value, &end, 0);
    if (errno || !*value || *end || *value == '-' || parsed > UINT32_MAX) {
        std::fprintf(stderr, "Invalid unsigned integer: %s\n", value);
        std::exit(1);
    }
    return static_cast<uint32_t>(parsed);
}

static void fill_input(uint32_t id, uint8_t* seed, uint8_t* rnd, uint8_t* msg) {
    if (id == 0) {
        for (unsigned i = 0; i < MLDSA_SEEDBYTES; ++i) { seed[i] = i + 1; }
        for (unsigned i = 0; i < MLDSA_RNDBYTES; ++i) { rnd[i] = 0x40 + i; }
        for (unsigned i = 0; i < MLDSA_MSG_BYTES; ++i) { msg[i] = 0x80 + i; }
        return;
    }
    uint32_t state = id;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<uint8_t>(state);
    };
    for (unsigned i = 0; i < MLDSA_SEEDBYTES; ++i) { seed[i] = next(); }
    for (unsigned i = 0; i < MLDSA_RNDBYTES; ++i) { rnd[i] = next(); }
    for (unsigned i = 0; i < MLDSA_MSG_BYTES; ++i) { msg[i] = next(); }
}

int main(int argc, char** argv) {
    const char* kernel_file = "kernel.vxbin";
    uint32_t requests = 1;
    uint32_t input_id = 0;
    int c;
    while ((c = getopt(argc, argv, "k:b:s:h")) != -1) {
        switch (c) {
        case 'k': kernel_file = optarg; break;
        case 'b': requests = parse_u32(optarg); break;
        case 's': input_id = parse_u32(optarg); break;
        default:
            std::cout << "Usage: [-k kernel] [-b requests] [-s first-input-id] [-h]\n";
            return c == 'h' ? 0 : 1;
        }
    }
    if (!requests || requests > VX_CFG_NUM_WARPS || input_id > UINT32_MAX - (requests - 1)) {
        std::fprintf(stderr, "Batch must fit the resident warp count and input IDs must not wrap\n");
        return 1;
    }

    std::cout << "ML-DSA-" << MLD_CONFIG_PARAMETER_SET
              << "  pk=" << MLDSA_PK_BYTES
              << " sk=" << MLDSA_SK_BYTES
              << " sig=" << MLDSA_SIG_BYTES << std::endl;

    STEP("device_open");
    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    const pqc::config cfg = pqc::print_config(dev, requests, VX_CFG_NUM_THREADS);
    if (cfg.cores != 1 || cfg.threads != VX_CFG_NUM_THREADS ||
        pqc::require_slots(cfg, requests, VX_CFG_NUM_THREADS) != 0) {
        std::fprintf(stderr, "Batch timing requires one core and matching resident warps\n");
        vx_device_release(dev);
        return 1;
    }
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    STEP("buffer_create");
    struct { vx_buffer_h h; uint64_t bytes; int write; } bufs[] = {
        { nullptr, MLDSA_SEEDBYTES,                    0 },  // seed
        { nullptr, MLDSA_RNDBYTES,                     0 },  // rnd
        { nullptr, MLDSA_MSG_BYTES,                    0 },  // msg
        { nullptr, MLDSA_PK_BYTES,                     1 },
        { nullptr, MLDSA_SK_BYTES,                     1 },
        { nullptr, MLDSA_SIG_BYTES,                    1 },
        { nullptr, MLDSA_ST_COUNT * sizeof(int32_t),   1 },
        { nullptr, MLDSA_CY_COUNT * sizeof(uint64_t),  1 },
        { nullptr, 4 * sizeof(uint32_t),               1 },  // arena peak/fail, stack peak/span
        { nullptr, MLD_PROF_COUNT * sizeof(uint32_t),  1 },  // call counts
        { nullptr, MLDSA_PROFILE_CY_COUNT * sizeof(uint64_t), 1 },
    };
    for (auto& b : bufs) {
        b.bytes *= requests;
        CHECK(vx_buffer_create(dev, b.bytes, b.write ? VX_MEM_WRITE : VX_MEM_READ, &b.h));
    }

    kernel_arg_t arg{};
    arg.requests = requests;
    uint64_t* slots[] = { &arg.seed_addr, &arg.rnd_addr, &arg.msg_addr,
                          &arg.pk_addr, &arg.sk_addr, &arg.sig_addr,
                          &arg.status_addr, &arg.cycles_addr, &arg.arena_addr,
                          &arg.counts_addr, &arg.pointwise_cycles_addr };
    for (size_t i = 0; i < sizeof(bufs)/sizeof(bufs[0]); ++i)
        CHECK(vx_buffer_address(bufs[i].h, slots[i]));

    STEP("module_load");
    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    STEP("upload inputs");
    std::vector<uint8_t> h_seed(requests * MLDSA_SEEDBYTES);
    std::vector<uint8_t> h_rnd(requests * MLDSA_RNDBYTES);
    std::vector<uint8_t> h_msg(requests * MLDSA_MSG_BYTES);
    for (unsigned req = 0; req < requests; ++req) {
        fill_input(input_id + req, h_seed.data() + req * MLDSA_SEEDBYTES,
                   h_rnd.data() + req * MLDSA_RNDBYTES,
                   h_msg.data() + req * MLDSA_MSG_BYTES);
    }
    CHECK(vx_enqueue_write(q, bufs[0].h, 0, h_seed.data(), h_seed.size(), 0, nullptr, nullptr));
    CHECK(vx_enqueue_write(q, bufs[1].h, 0, h_rnd.data(),  h_rnd.size(),  0, nullptr, nullptr));
    CHECK(vx_enqueue_write(q, bufs[2].h, 0, h_msg.data(),  h_msg.size(),  0, nullptr, nullptr));

    std::vector<int32_t> h_st(requests * MLDSA_ST_COUNT, -12345);
    CHECK(vx_enqueue_write(q, bufs[6].h, 0, h_st.data(), h_st.size()*sizeof(int32_t), 0, nullptr, nullptr));

    STEP("launch");
    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = requests; li.block_dim[0] = VX_CFG_NUM_THREADS;
    vx_event_h lev = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    STEP("readback");
    std::vector<uint64_t> h_cy(requests * MLDSA_CY_COUNT, 0);
    std::vector<uint32_t> h_ar(requests * 4, 0);
    std::vector<uint32_t> h_cnt(requests * MLD_PROF_COUNT, 0);
    std::vector<uint64_t> h_pw(requests * MLDSA_PROFILE_CY_COUNT, 0);
    vx_event_h e1=nullptr, e2=nullptr, e3=nullptr, e4=nullptr, e5=nullptr;
    CHECK(vx_enqueue_read(q, h_st.data(), bufs[6].h, 0, h_st.size()*sizeof(int32_t), 1, &lev, &e1));
    CHECK(vx_enqueue_read(q, h_cy.data(), bufs[7].h, 0, h_cy.size()*sizeof(uint64_t), 1, &lev, &e2));
    CHECK(vx_enqueue_read(q, h_ar.data(), bufs[8].h, 0, h_ar.size()*sizeof(uint32_t), 1, &lev, &e3));
    CHECK(vx_enqueue_read(q, h_cnt.data(), bufs[9].h, 0, h_cnt.size()*sizeof(uint32_t), 1, &lev, &e4));
    CHECK(vx_enqueue_read(q, h_pw.data(), bufs[10].h, 0, h_pw.size()*sizeof(uint64_t), 1, &lev, &e5));
    STEP("wait");
    CHECK(vx_event_wait_value(e1, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e2, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e3, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e4, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e5, 1, VX_TIMEOUT_INFINITE));

    STEP("verify");
    int errors = 0;
    uint64_t first_start = std::numeric_limits<uint64_t>::max();
    uint64_t last_end = 0;
    for (unsigned req = 0; req < requests; ++req) {
        auto st = h_st.data() + req * MLDSA_ST_COUNT;
        auto cy = h_cy.data() + req * MLDSA_CY_COUNT;
        auto ar = h_ar.data() + req * 4;
        auto cnt = h_cnt.data() + req * MLD_PROF_COUNT;
        auto pw = h_pw.data() + req * MLDSA_PROFILE_CY_COUNT;
        std::printf("REQUEST: id=%u input=%u\n", req, input_id + req);
        static const char* step_name[MLDSA_ST_COUNT] = { "keypair_internal", "signature_internal", "verify_internal" };
#if defined(PQC_ABLATE_KECCAK) || defined(PQC_ABLATE_NTT)
        std::printf("ABLATION BUILD -- results are invalid by construction\n");
        (void)step_name;
#else
        for (int i = 0; i < MLDSA_ST_COUNT; ++i) {
            if (st[i] != 0) {
                std::printf("*** %s returned %d%s\n", step_name[i], st[i],
                            st[i] == -12345 ? "  (kernel never ran)" : "");
                ++errors;
            }
        }
        std::vector<uint8_t> ref_pk(MLDSA_PK_BYTES), ref_sk(MLDSA_SK_BYTES), ref_sig(MLDSA_SIG_BYTES);
        if (mldsa_host_reference(h_seed.data() + req * MLDSA_SEEDBYTES,
                                 h_rnd.data() + req * MLDSA_RNDBYTES,
                                 h_msg.data() + req * MLDSA_MSG_BYTES, MLDSA_MSG_BYTES,
                                ref_pk.data(), ref_sk.data(), ref_sig.data()) != 0) {
            std::printf("*** portable host reference failed\n");
            ++errors;
        } else {
            const std::vector<uint8_t>* reference[] = { &ref_pk, &ref_sk, &ref_sig };
            const char* names[] = { "pk", "sk", "signature" };
            for (int i = 0; i < 3; ++i) {
                std::vector<uint8_t> got(reference[i]->size());
                vx_event_h read = nullptr;
                CHECK(vx_enqueue_read(q, got.data(), bufs[3+i].h, req * got.size(), got.size(), 1, &lev, &read));
                CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));
                vx_event_release(read);
                if (got != *reference[i]) {
                    std::printf("*** %s differs from portable host reference\n", names[i]);
                    ++errors;
                }
            }
            if (!errors) std::printf("REFERENCE: pk/sk/signature match portable C byte-for-byte\n");
        }
#endif
        if (ar[1] != 0) {
            std::printf("*** arena allocation/release failures: %u\n", ar[1]);
            ++errors;
        }

        // A peak of zero means MLD_CUSTOM_ALLOC never fired, which -- because the
        // library then allocated somewhere this build cannot see -- reads as a
        // clean pass with a broken instrument. That happened once already, when the
        // library and the kernel were separate translation units and each got its
        // own copy of the file-scope arena. An instrument that must read nonzero
        // and reads zero is a failure, not a footnote.
        if (ar[0] == 0) {
            std::printf("*** arena peak is zero -- the custom allocator was never "
                        "reached, so these results describe a build this test is "
                        "not measuring\n");
            ++errors;
        }
#if defined(PQC_KEYPAIR_ONLY)
        if (cy[MLDSA_CY_KEYPAIR] == 0) { std::printf("*** keypair measured zero cycles\n"); ++errors; }
#else
        for (int i = 0; i < MLDSA_ST_COUNT; ++i)
            if (cy[i] == 0) { std::printf("*** phase %d measured zero cycles\n", i); ++errors; }
#endif

        if (ar[2] == 0) {
            std::printf("*** stack watermark is zero -- the probe never ran\n");
            ++errors;
        } else if (ar[2] >= ar[3]) {
            std::printf("*** stack peak %u B filled its whole %u B paintable slab"
                        " -- it overflowed into the next hart\n", ar[2], ar[3]);
            ++errors;
        }
        std::printf("STACK: peak=%u of %u paintable (slab %u)   ARENA: peak=%u of %u fail=%u\n",
                    ar[2], ar[3], (unsigned)PQC_SLAB_BYTES,
                    ar[0], (unsigned)MLD_ARENA_BYTES, ar[1]);
        static const char* pname[MLD_PROF_ARM] = {
            "keccak_f1600_x1", "keccak_f1600_x4", "poly_ntt", "poly_invntt",
            "rej_uniform", "poly_pointwise", "pointwise_acc_l5"
        };
        std::printf("%-18s %10s\n", "primitive", "calls");
        for (int i = 0; i < MLD_PROF_ARM; ++i)
            std::printf("%-18s %10u\n", pname[i], cnt[i]);
        if (cnt[MLD_PROF_KECCAK_X1] == 0) {
            std::printf("*** keccak_f1600_x1 was never called -- hook not wired in\n");
            ++errors;
        }

        const uint32_t dev_arm = cnt[MLD_PROF_ARM];
        const uint32_t host_arm = MLD_ARM_EXPECTED;
        const char* backend = (dev_arm & MLD_ARM_KECCAK_PE) ? "pe"
                           : (dev_arm & MLD_ARM_KECCAK_KROUND25) ? "kround"
                           : (dev_arm & MLD_ARM_KECCAK_SG25_SW) ? "sg25_sw"
                           : (dev_arm & MLD_ARM_KECCAK_SG25) ? "sg25" : "sg1";
        const char* ntt = (dev_arm & MLD_ARM_NTT_REG32) ? "reg32" : "c";
        std::printf("ARM: keccak=%s unroll=%u ntt=%s nttmul=%s nttbf=%s pointwise=%s l5=%s ablate=%s\n", backend,
                    unsigned((dev_arm & MLD_ARM_KECCAK_UNROLL) != 0),
                    ntt, (dev_arm & MLD_ARM_NTTMUL_D) ? "d" : "c",
                    (dev_arm & MLD_ARM_NTTBF_D) ? "d" : "shuffle",
                    (dev_arm & MLD_ARM_POINTWISE_ISE) ? "ise" : "c",
                    (dev_arm & MLD_ARM_POINTWISE_L5_W32) ? "w32" : "c",
                    (dev_arm & MLD_ARM_ABLATE_KECCAK) ? "keccak"
                      : (dev_arm & MLD_ARM_ABLATE_NTT) ? "ntt" : "none");
        if (dev_arm != host_arm) {
            std::printf("*** arm mismatch: device 0x%x, host 0x%x -- one side was "
                        "built without the other's flags\n", dev_arm, host_arm);
            ++errors;
        }

        const uint64_t total = cy[0] + cy[1] + cy[2];
        std::printf("CYCLES: keypair=%llu sign=%llu verify=%llu total=%llu\n",
                    (unsigned long long)cy[0], (unsigned long long)cy[1],
                    (unsigned long long)cy[2], (unsigned long long)total);
        static const char* phase_name[MLDSA_ST_COUNT] = { "keypair", "sign", "verify" };
        for (unsigned phase = 0; phase < MLDSA_ST_COUNT; ++phase) {
            const uint64_t simple = pw[phase * 2];
            const uint64_t l5 = pw[phase * 2 + 1];
            std::printf("POINTWISE: phase=%s simple=%llu l5=%llu share=%.2f%%\n",
                        phase_name[phase], (unsigned long long)simple,
                        (unsigned long long)l5,
                        cy[phase] ? 100.0 * (simple + l5) / cy[phase] : 0.0);
#if defined(PQC_PROFILE_PHASES)
            const auto detail = pw + MLDSA_POINTWISE_CY_COUNT + phase * MLD_PHASE_COUNT;
            const uint64_t named = detail[MLD_PHASE_PERMUTE] + detail[MLD_PHASE_NTT]
                                 + detail[MLD_PHASE_INTT] + simple + l5;
            if (named > cy[phase]) {
                std::printf("*** phase detail exceeds %s interval\n", phase_name[phase]);
                ++errors;
            }
            std::printf("DETAIL: phase=%s permute=%llu ntt=%llu intt=%llu pointwise=%llu residual=%llu total=%llu\n",
                        phase_name[phase],
                        (unsigned long long)detail[MLD_PHASE_PERMUTE],
                        (unsigned long long)detail[MLD_PHASE_NTT],
                        (unsigned long long)detail[MLD_PHASE_INTT],
                        (unsigned long long)(simple + l5),
                        (unsigned long long)(named <= cy[phase] ? cy[phase] - named : 0),
                        (unsigned long long)cy[phase]);
#endif
        }

        first_start = std::min(first_start, cy[MLDSA_CY_START]);
        last_end = std::max(last_end, cy[MLDSA_CY_END]);
    }
    std::printf("BATCH: requests=%u first_input=%u makespan=%llu\n",
                requests, input_id, (unsigned long long)(last_end - first_start));
    if (std::getenv("VORTEX_PROFILING")) {
        CHECK(vx_device_dump_perf(dev, stdout));
    }

    vx_event_release(e1); vx_event_release(e2); vx_event_release(e3);
    vx_event_release(e4); vx_event_release(e5); vx_event_release(lev);
    for (auto& b : bufs) vx_buffer_release(b.h);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
