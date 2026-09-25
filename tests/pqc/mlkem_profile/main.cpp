// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// profile -- exact primitive call counts for one ML-KEM round trip.
//
// Combined with the per-call costs from tests/pqc/mlkem_microbench, this turns the
// end-to-end 33M cycles into an attribution: how much of it is Keccak, how much
// is the NTT, how much is sampling. That attribution is what decides which
// instruction to design first.

#include <vortex2.h>
#include <mlkem_native.h>
#include "common.h"
#include "expected_test_vectors.h"
#include "pqc_config.h"

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
const Cost kCost[MLK_PROF_ARM] = {
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
    uint32_t requests = 1;
    uint32_t workers = 0;
    uint32_t ntt_lanes = 1;
    int c;
    while ((c = getopt(argc, argv, "k:b:t:w:h")) != -1) {
        if (c == 'k') {
            kernel_file = optarg;
        } else if (c == 'b') {
            requests = (uint32_t)std::atoi(optarg);
        } else if (c == 'w') {
            workers = (uint32_t)std::atoi(optarg);
        } else if (c == 't') {
            ntt_lanes = (uint32_t)std::atoi(optarg);
        } else {
            std::cout << "Usage: [-k kernel] [-b requests] [-w resident-workers] [-t NTT lanes] [-h]" << std::endl;
            std::exit(c == 'h' ? 0 : -1);
        }
    }
    if (requests < 1) {
        std::fprintf(stderr, "FAIL: -b must be >= 1\n");
        return 1;
    }
#if defined(PQC_PROFILE_PHASES)
    if (requests != 1) {
        std::fprintf(stderr, "FAIL: PROFILE_PHASES requires -b 1; per-request intervals overlap at M8\n");
        return 1;
    }
#endif
    if (ntt_lanes < 1 || ntt_lanes > 32 || (ntt_lanes & (ntt_lanes - 1)) != 0) {
        std::fprintf(stderr, "FAIL: NTT lanes must be a power of two between 1 and 32\n");
        return 1;
    }
#if !defined(PQC_NTT_COOP)
    if (ntt_lanes != 1) {
        std::fprintf(stderr, "FAIL: multiple NTT lanes require NTT=coop\n");
        return 1;
    }
#endif
#if defined(PQC_NTT_REG32) || defined(PQC_NTT_SMEM32)
    if (ntt_lanes != 32) {
        std::fprintf(stderr, "FAIL: W32 NTT requires -t 32\n");
        return 1;
    }
#endif

    if (!workers) workers = requests < VX_CFG_NUM_WARPS ? requests : VX_CFG_NUM_WARPS;
    if (workers > requests || workers > VX_CFG_NUM_WARPS || requests % workers != 0) {
        std::fprintf(stderr, "Requests must be a multiple of 1..NUM_WARPS resident workers\n");
        return 1;
    }
    std::printf("SCHEDULING: requests=%u workers=%u waves=%u\n",
                requests, workers, requests / workers);

    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    const pqc::config cfg = pqc::print_config(dev, requests, ntt_lanes);
    std::printf("PARAMETER_SET: ML-KEM-%u\n", MLK_CONFIG_PARAMETER_SET);
    std::printf("MAPPING: KEM_lanes=1 NTT_lanes=%u\n", ntt_lanes);
    if (cfg.cores != 1) {
        std::fprintf(stderr, "FAIL: batch timestamps require one shared core cycle counter\n");
        vx_device_release(dev);
        return 1;
    }
    if (pqc::require_slots(cfg, workers, ntt_lanes) != 0) {
        vx_device_release(dev);
        return 1;
    }
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    vx_buffer_h scr = nullptr, cnt = nullptr, st = nullptr, cyc = nullptr;
    CHECK(vx_buffer_create(dev, (uint64_t)requests * P_SCRATCH_LEN, VX_MEM_WRITE, &scr));
    CHECK(vx_buffer_create(dev, (uint64_t)requests * MLK_PROF_COUNT * sizeof(uint32_t), VX_MEM_WRITE, &cnt));
    CHECK(vx_buffer_create(dev, (uint64_t)requests * 3 * sizeof(int32_t), VX_MEM_WRITE, &st));
    CHECK(vx_buffer_create(dev, (uint64_t)requests * P_CYCLE_COUNT * sizeof(uint64_t), VX_MEM_WRITE, &cyc));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(scr, &arg.scratch_addr));
    CHECK(vx_buffer_address(cnt, &arg.counts_addr));
    CHECK(vx_buffer_address(st,  &arg.status_addr));
    CHECK(vx_buffer_address(cyc, &arg.cycles_addr));
    arg.requests = requests;
    arg.workers = workers;
    arg.ntt_lanes = ntt_lanes;

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    // The FIPS 203 KAT coins -- the same d || z || m that tests/pqc/mlkem uploads
    // (main.cpp:136-138), landing on the same scratch offsets (P_OFF_COINS_KP is
    // 64 bytes of d || z, P_OFF_COINS_ENC is 32 bytes of m).
    //
    // This used to be h_scr[i] = i, which is reproducible but is a DIFFERENT INPUT
    // from the baseline. Rejection sampling makes the Keccak permutation count
    // depend on the bytes drawn: those coins landed on 156 permutations, a 6.28%
    // tail seed, while the baseline's KAT coins land on 144, the mode at 92.6% of
    // 20,000 host seeds. Every Amdahl figure taken here was therefore measured on
    // one input and quoted against a baseline measured on another -- the mixing
    // next_steps_recipes.md:996 forbids by name, and the same mixing that produced
    // the retired 72.3%. A profile is supposed to describe its baseline, so this is
    // a correctness fix and not a sampling choice.
    std::vector<uint8_t> h_scr((size_t)requests * P_SCRATCH_LEN, 0);
    for (uint32_t req = 0; req < requests; ++req) {
        auto s = h_scr.data() + (size_t)req * P_SCRATCH_LEN;
        std::memcpy(s + P_OFF_COINS_KP, test_vector_d, 32);
        std::memcpy(s + P_OFF_COINS_KP + 32, test_vector_z, 32);
        std::memcpy(s + P_OFF_COINS_ENC, test_vector_m, 32);
    }
    CHECK(vx_enqueue_write(q, scr, 0, h_scr.data(), h_scr.size(), 0, nullptr, nullptr));

    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = workers; li.block_dim[0] = ntt_lanes;
#if defined(PQC_NTT_SMEM32)
    li.lmem_size = 288u * sizeof(uint32_t);
#endif
    vx_event_h lev = nullptr, e1 = nullptr, e2 = nullptr, e3 = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    std::vector<uint32_t> h_cnt((size_t)requests * MLK_PROF_COUNT, 0);
    std::vector<int32_t>  h_st((size_t)requests * 3, -1);
    CHECK(vx_enqueue_read(q, h_cnt.data(), cnt, 0, h_cnt.size() * sizeof(uint32_t), 1, &lev, &e1));
    CHECK(vx_enqueue_read(q, h_st.data(),  st,  0, h_st.size()  * sizeof(int32_t),  1, &lev, &e2));
    CHECK(vx_event_wait_value(e1, 1, VX_TIMEOUT_INFINITE));
    std::vector<uint64_t> h_cyc((size_t)requests * P_CYCLE_COUNT, 0);
    CHECK(vx_enqueue_read(q, h_cyc.data(), cyc, 0, h_cyc.size() * sizeof(uint64_t), 1, &lev, &e3));
    CHECK(vx_event_wait_value(e2, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e3, 1, VX_TIMEOUT_INFINITE));

#if !defined(PQC_ABLATE_KECCAK) && !defined(PQC_ABLATE_NTT)
    vx_event_h read = nullptr;
    CHECK(vx_enqueue_read(q, h_scr.data(), scr, 0, h_scr.size(), 1, &lev, &read));
    CHECK(vx_event_wait_value(read, 1, VX_TIMEOUT_INFINITE));
    vx_event_release(read);
    const struct {
        const char* name;
        size_t offset;
        const uint8_t* expected;
        size_t size;
    } vectors[] = {
        { "pk", P_OFF_PK, test_vector_pk, sizeof(test_vector_pk) },
        { "sk", P_OFF_SK, test_vector_sk, sizeof(test_vector_sk) },
        { "ct", P_OFF_CT, test_vector_ct, sizeof(test_vector_ct) },
        { "ss_enc", P_OFF_SS_ENC, test_vector_ss, sizeof(test_vector_ss) },
        { "ss_dec", P_OFF_SS_DEC, test_vector_ss, sizeof(test_vector_ss) },
    };
#endif

    int errors = 0;
    uint64_t first_start = UINT64_MAX, last_end = 0;
    for (uint32_t req = 0; req < requests; ++req) {
        const auto counts = h_cnt.data() + (size_t)req * MLK_PROF_COUNT;
        const auto status = h_st.data() + (size_t)req * 3;
        const auto cycles = h_cyc.data() + (size_t)req * P_CYCLE_COUNT;
        const uint32_t arm = counts[MLK_PROF_ARM];
        const char* ntt = (arm & MLK_ARM_NTT_SMEM32) ? "smem32" :
                          (arm & MLK_ARM_NTT_REG32) ? "reg32" :
                          (arm & MLK_ARM_NTT_COOP) ? "coop" : "c";
        const char* nttbf = (arm & MLK_ARM_NTTBF_K) ? "sg2" :
                            (arm & MLK_ARM_NTT_REG32) ? "shfl" : "none";
        const char* keccak = (arm & MLK_ARM_KECCAK_KROUND25) ? "kround" :
                             (arm & MLK_ARM_KECCAK_ASM) ? "asm" :
                             (arm & MLK_ARM_KECCAK_SG25) ? "sg25" :
                             (arm & MLK_ARM_KECCAK_SG25_SW) ? "sg25_sw" :
                             (arm & MLK_ARM_KECCAK_SG1) ? "sg1" :
                             (arm & MLK_ARM_KECCAK_PE) ? "pe" : "c";
        std::printf("ARM: id=%u keccak=%s fips202=%s ablate=%s ntt=%s nttmul=%s nttbf=%s flags=%u unroll=%u\n", req,
                    keccak, (arm & MLK_ARM_SERIAL_FIPS202) ? "serial" : "default",
                    (arm & MLK_ARM_ABLATE_NTT) ? "ntt" :
                    (arm & MLK_ARM_ABLATE_KECCAK) ? "keccak" : "none",
                    ntt, (arm & MLK_ARM_NTTMUL_K) ? "k" : "c", nttbf, arm,
                    (unsigned)((arm & (MLK_ARM_KECCAK_UNROLL | MLK_ARM_KECCAK_KROUND25)) != 0));
        std::printf("SAMPLER_ARM: id=%u matrix=%s\n", req,
                    (arm & MLK_ARM_REJ_WARP) ? "warp" : "scalar");
        std::printf("CODEC_ARM: id=%u polybytes=%s compress=%s\n", req,
                    (arm & MLK_ARM_CODEC_WARP) ? "warp" : "scalar",
                    (arm & MLK_ARM_COMPRESS_WARP) ? "warp" : "scalar");
        std::printf("NOISE_ARM: id=%u cbd=%s\n", req,
                    (arm & MLK_ARM_NOISE_WARP) ? "warp" : "scalar");
        std::printf("LINEAR_ARM: id=%u poly=%s\n", req,
                    (arm & MLK_ARM_LINEAR_WARP) ? "warp" : "scalar");
        std::printf("ZEROIZE_ARM: id=%u single=%s multi=scalar\n", req,
                    (arm & MLK_ARM_ZEROIZE_WARP) ? "warp" : "scalar");
#if defined(PQC_PROFILE_PHASES)
        std::printf("PHASE_ARM: id=%u direct_intervals=1\n", req);
#endif
#if defined(PQC_ARITH_COOP) || defined(PQC_PROFILE_ARITH)
        std::printf("ARITH_ARM: id=%u mulcache=%s basemul=%s reduce=%s mul=%s profile=%u\n",
                    req, (arm & MLK_ARM_ARITH_MULCACHE) ? "w32" : "c",
                    (arm & MLK_ARM_ARITH_BASEMUL) ? "w32" : "c",
                    (arm & MLK_ARM_ARITH_REDUCE) ? "w32" : "c",
                    (arm & MLK_ARM_ARITH_NTTMUL) ? "ise" : "c",
                    (unsigned)((arm & MLK_ARM_PROFILE_ARITH) != 0));
#endif
        if (arm != MLK_ARM_EXPECTED) {
            std::printf("*** request %u: kernel arm differs from host: expected %u\n",
                        req, MLK_ARM_EXPECTED);
            ++errors;
        }
        std::printf("STATUS: id=%u keypair=%d encaps=%d decaps=%d\n",
                    req, status[0], status[1], status[2]);
#if !defined(PQC_ABLATE_KECCAK)
        for (int i = 0; i < 3; ++i) {
            if (status[i] != 0) {
                std::printf("*** request %u: KEM step %d returned %d\n", req, i, status[i]);
                ++errors;
            }
        }
#endif
#if defined(PQC_ABLATE_KECCAK) || defined(PQC_ABLATE_NTT)
        std::printf("ABLATION BUILD: id=%u -- results are invalid by construction\n", req);
#else
        const auto s = h_scr.data() + (size_t)req * P_SCRATCH_LEN;
        bool kat_ok = true;
        for (const auto& v : vectors) {
            if (std::memcmp(s + v.offset, v.expected, v.size) != 0) {
                std::printf("*** request %u: %s differs from known-answer vector\n", req, v.name);
                kat_ok = false;
                ++errors;
            }
        }
        if (kat_ok) {
            std::printf("KAT: id=%u pk/sk/ct/ss_enc/ss_dec match byte-for-byte\n", req);
        }
#endif

        std::printf("COUNTERS: id=%u keccak_x1=%u keccak_x4=%u ntt=%u intt=%u "
                    "rej_uniform=%u mulcache=%u basemul=%u poly_reduce=%u\n", req,
                    counts[MLK_PROF_KECCAK_X1], counts[MLK_PROF_KECCAK_X4],
                    counts[MLK_PROF_NTT], counts[MLK_PROF_INTT],
                    counts[MLK_PROF_REJ_UNIFORM], counts[MLK_PROF_MULCACHE],
                    counts[MLK_PROF_BASEMUL], counts[MLK_PROF_POLY_REDUCE]);
        std::printf("REFERENCE C MICROBENCH ESTIMATES (not measured attribution):\n");
        std::printf("%-18s %10s %14s\n", "primitive", "calls", "est_cycles");
        double attributed = 0;
        for (int i = 0; i < MLK_PROF_ARM; ++i) {
            const double est = counts[i] * kCost[i].cycles;
            attributed += est;
            if (kCost[i].cycles > 0) {
                std::printf("%-18s %10u %14.0f\n", kCost[i].name, counts[i], est);
            } else {
                std::printf("%-18s %10u %14s\n", kCost[i].name, counts[i], "-");
            }
            if (counts[i] != h_cnt[i]) {
                std::printf("*** request %u: %s count differs from request 0\n", req, kCost[i].name);
                ++errors;
            }
        }
        static const int must_be_called[] = {
            MLK_PROF_KECCAK_X1, MLK_PROF_NTT, MLK_PROF_INTT, MLK_PROF_REJ_UNIFORM
        };
        for (int i : must_be_called) {
            if (counts[i] == 0) {
                std::printf("*** request %u: %s was never called -- its hook is not wired in\n",
                            req, kCost[i].name);
                ++errors;
            }
        }

        std::printf("reference estimate total: %.0f\n", attributed);
        for (int i = 0; i < 3; ++i) {
            if (cycles[i] == 0) {
                std::printf("*** request %u: phase %d measured zero cycles\n", req, i);
                ++errors;
            }
        }
        const uint64_t total = cycles[0] + cycles[1] + cycles[2];
#if defined(PQC_PROFILE_ARITH)
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned index = MLK_PROF_MULCACHE + i;
            const uint64_t elapsed = cycles[P_CYCLE_MULCACHE + i];
            std::printf("ARITH_PROFILE: id=%u primitive=%s calls=%u cycles=%llu "
                        "per_request_elapsed_pct=%.3f\n", req, kCost[index].name,
                        counts[index], (unsigned long long)elapsed,
                        100.0 * elapsed / total);
            if (counts[index] == 0 || elapsed == 0 || elapsed > total) {
                std::printf("*** request %u: invalid %s timing\n", req, kCost[index].name);
                ++errors;
            }
        }
#endif
#if defined(PQC_PROFILE_PHASES)
        const struct {
            const char* name;
            unsigned cycle;
        } phases[] = {
            { "keccak_permute", P_CYCLE_PERMUTE },
            { "keccak_absorb", P_CYCLE_ABSORB },
            { "keccak_squeeze", P_CYCLE_SQUEEZE },
            { "ntt", P_CYCLE_NTT },
            { "intt", P_CYCLE_INTT },
            { "rejection", P_CYCLE_REJECTION },
            { "codec", P_CYCLE_CODEC },
            { "noise", P_CYCLE_NOISE },
#if defined(PQC_LINEAR_WARP)
            { "linear", P_CYCLE_LINEAR },
#endif
#if defined(PQC_ZEROIZE_WARP)
            { "zeroize", P_CYCLE_ZEROIZE },
#endif
            { "mulcache", P_CYCLE_MULCACHE },
            { "basemul", P_CYCLE_BASEMUL },
            { "reduce", P_CYCLE_REDUCE },
        };
        uint64_t profiled = 0;
        for (const auto& phase : phases) {
            const uint64_t elapsed = cycles[phase.cycle];
            profiled += elapsed;
            std::printf("PHASE_PROFILE: id=%u phase=%s cycles=%llu pct=%.3f\n",
                        req, phase.name, (unsigned long long)elapsed,
                        100.0 * elapsed / total);
            if (elapsed == 0 || elapsed > total) {
                std::printf("*** request %u: invalid %s phase timing\n", req, phase.name);
                ++errors;
            }
        }
        if (profiled > total) {
            std::printf("*** request %u: phase timings overlap total cycles\n", req);
            ++errors;
        } else {
            const uint64_t residual = total - profiled;
            std::printf("PHASE_PROFILE: id=%u phase=other cycles=%llu pct=%.3f\n",
                        req, (unsigned long long)residual,
                        100.0 * residual / total);
            std::printf("PHASE_SUM: id=%u measured=%llu other=%llu total=%llu\n",
                        req, (unsigned long long)profiled,
                        (unsigned long long)residual,
                        (unsigned long long)total);
        }
#endif
        const uint64_t start = cycles[P_CYCLE_START], end = cycles[P_CYCLE_END];
        if (end <= start || end - start != total) {
            std::printf("*** request %u: timestamps disagree with phase cycles\n", req);
            ++errors;
        }
        if (start < first_start) {
            first_start = start;
        }
        if (end > last_end) {
            last_end = end;
        }
        std::printf("REQUEST: id=%u arm=%u keypair=%llu encaps=%llu decaps=%llu "
                    "total=%llu start=%llu end=%llu\n", req, arm,
                    (unsigned long long)cycles[0], (unsigned long long)cycles[1],
                    (unsigned long long)cycles[2], (unsigned long long)total,
                    (unsigned long long)start, (unsigned long long)end);
        if (requests == 1) {
            std::printf("CYCLES: keypair=%llu encaps=%llu decaps=%llu total=%llu\n",
                        (unsigned long long)cycles[0], (unsigned long long)cycles[1],
                        (unsigned long long)cycles[2], (unsigned long long)total);
        }
    }
    std::printf("BATCH: requests=%u start=%llu end=%llu makespan=%llu\n", requests,
                (unsigned long long)first_start, (unsigned long long)last_end,
                (unsigned long long)(last_end - first_start));

    vx_device_dump_perf(dev, stdout);

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
