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
    int c;
    while ((c = getopt(argc, argv, "k:h")) != -1) {
        if (c == 'k') kernel_file = optarg;
        else { std::cout << "Usage: [-k kernel] [-h]" << std::endl; std::exit(c == 'h' ? 0 : -1); }
    }

    std::cout << "ML-DSA-" << MLD_CONFIG_PARAMETER_SET
              << "  pk=" << MLDSA_PK_BYTES
              << " sk=" << MLDSA_SK_BYTES
              << " sig=" << MLDSA_SIG_BYTES << std::endl;

    STEP("device_open");
    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
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
        { nullptr, 2 * sizeof(uint32_t),               1 },  // arena peak, fail
    };
    for (auto& b : bufs)
        CHECK(vx_buffer_create(dev, b.bytes, b.write ? VX_MEM_WRITE : VX_MEM_READ, &b.h));

    kernel_arg_t arg{};
    uint64_t* slots[] = { &arg.seed_addr, &arg.rnd_addr, &arg.msg_addr,
                          &arg.pk_addr, &arg.sk_addr, &arg.sig_addr,
                          &arg.status_addr, &arg.cycles_addr, &arg.arena_addr };
    for (size_t i = 0; i < sizeof(bufs)/sizeof(bufs[0]); ++i)
        CHECK(vx_buffer_address(bufs[i].h, slots[i]));

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

    std::vector<int32_t> h_st(MLDSA_ST_COUNT, -12345);
    CHECK(vx_enqueue_write(q, bufs[6].h, 0, h_st.data(), h_st.size()*sizeof(int32_t), 0, nullptr, nullptr));

    STEP("launch");
    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = 1; li.block_dim[0] = 1;
    vx_event_h lev = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &lev));

    STEP("readback");
    std::vector<uint64_t> h_cy(MLDSA_CY_COUNT, 0);
    std::vector<uint32_t> h_ar(2, 0);
    vx_event_h e1=nullptr, e2=nullptr, e3=nullptr;
    CHECK(vx_enqueue_read(q, h_st.data(), bufs[6].h, 0, h_st.size()*sizeof(int32_t), 1, &lev, &e1));
    CHECK(vx_enqueue_read(q, h_cy.data(), bufs[7].h, 0, h_cy.size()*sizeof(uint64_t), 1, &lev, &e2));
    CHECK(vx_enqueue_read(q, h_ar.data(), bufs[8].h, 0, h_ar.size()*sizeof(uint32_t), 1, &lev, &e3));
    STEP("wait");
    CHECK(vx_event_wait_value(e1, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e2, 1, VX_TIMEOUT_INFINITE));
    CHECK(vx_event_wait_value(e3, 1, VX_TIMEOUT_INFINITE));

    STEP("verify");
    int errors = 0;
    static const char* step_name[MLDSA_ST_COUNT] = { "keypair_internal", "signature_internal", "verify_internal" };
    for (int i = 0; i < MLDSA_ST_COUNT; ++i) {
        if (h_st[i] != 0) {
            std::printf("*** %s returned %d%s\n", step_name[i], h_st[i],
                        h_st[i] == -12345 ? "  (kernel never ran)" : "");
            ++errors;
        }
    }
    if (h_ar[1] != 0) {
        std::printf("*** arena exhausted %u times -- raise MLD_ARENA_BYTES\n", h_ar[1]);
        ++errors;
    }

    // A peak of zero means MLD_CUSTOM_ALLOC never fired, which -- because the
    // library then allocated somewhere this build cannot see -- reads as a
    // clean pass with a broken instrument. That happened once already, when the
    // library and the kernel were separate translation units and each got its
    // own copy of the file-scope arena. An instrument that must read nonzero
    // and reads zero is a failure, not a footnote.
    if (h_ar[0] == 0) {
        std::printf("*** arena peak is zero -- the custom allocator was never "
                    "reached, so these results describe a build this test is "
                    "not measuring\n");
        ++errors;
    }
    for (int i = 0; i < MLDSA_CY_COUNT; ++i)
        if (h_cy[i] == 0) { std::printf("*** phase %d measured zero cycles\n", i); ++errors; }

    std::printf("ARENA: peak=%u bytes of %u\n", h_ar[0], (unsigned)(128*1024));
    const uint64_t total = h_cy[0] + h_cy[1] + h_cy[2];
    std::printf("CYCLES: keypair=%llu sign=%llu verify=%llu total=%llu\n",
                (unsigned long long)h_cy[0], (unsigned long long)h_cy[1],
                (unsigned long long)h_cy[2], (unsigned long long)total);

    vx_event_release(e1); vx_event_release(e2); vx_event_release(e3); vx_event_release(lev);
    for (auto& b : bufs) vx_buffer_release(b.h);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
