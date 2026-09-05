// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// microbench -- per-primitive cost of the ML-KEM software baseline on Vortex.
//
// The end-to-end test says one KEM round trip costs ~33M cycles; this says what
// a single Keccak permutation, NTT, inverse NTT or rejection sample costs. The
// two together are what turns "ML-KEM is slow" into "N calls x C cycles here",
// which is the only form an ISA decision can be made from.

#include <vortex2.h>
#include "common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
uint32_t    iters       = 16;

void parse_args(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "n:k:h")) != -1) {
        switch (c) {
            case 'n': iters       = std::atoi(optarg); break;
            case 'k': kernel_file = optarg;            break;
            default:
                std::cout << "Usage: [-n iters] [-k kernel] [-h]" << std::endl;
                std::exit(c == 'h' ? 0 : -1);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    parse_args(argc, argv);
    std::cout << "microbench: " << iters << " iterations per primitive" << std::endl;

    vx_device_h dev = nullptr;
    CHECK(vx_device_open(0, &dev));
    vx_queue_info_t qi = { sizeof(qi), nullptr, VX_QUEUE_PRIORITY_NORMAL, 0 };
    vx_queue_h q = nullptr;
    CHECK(vx_queue_create(dev, &qi, &q));

    vx_buffer_h cyc_buf = nullptr, scr_buf = nullptr;
    CHECK(vx_buffer_create(dev, MB_COUNT * sizeof(uint64_t), VX_MEM_WRITE, &cyc_buf));
    CHECK(vx_buffer_create(dev, MB_SCRATCH_LEN, VX_MEM_WRITE, &scr_buf));

    kernel_arg_t arg{};
    arg.iters = iters;
    CHECK(vx_buffer_address(cyc_buf, &arg.cycles_addr));
    CHECK(vx_buffer_address(scr_buf, &arg.scratch_addr));

    vx_module_h mod = nullptr; vx_kernel_h kern = nullptr;
    CHECK(vx_module_load_file(dev, kernel_file, &mod));
    CHECK(vx_module_get_kernel(mod, "main", &kern));

    // Zero the scratch so a run is reproducible: rejection sampling's cost
    // depends on how many candidates it rejects, which depends on these bytes.
    std::vector<uint8_t> zeros(MB_SCRATCH_LEN, 0);
    CHECK(vx_enqueue_write(q, scr_buf, 0, zeros.data(), zeros.size(), 0, nullptr, nullptr));

    vx_launch_info_t li{};
    li.struct_size = sizeof(li); li.kernel = kern;
    li.args_host = &arg; li.args_size = sizeof(arg);
    li.ndim = 1; li.grid_dim[0] = 1; li.block_dim[0] = 1;
    vx_event_h launch_ev = nullptr, read_ev = nullptr;
    CHECK(vx_enqueue_launch(q, &li, 0, nullptr, &launch_ev));

    std::vector<uint64_t> h_cycles(MB_COUNT, 0);
    CHECK(vx_enqueue_read(q, h_cycles.data(), cyc_buf, 0,
                          h_cycles.size() * sizeof(uint64_t), 1, &launch_ev, &read_ev));
    CHECK(vx_event_wait_value(read_ev, 1, VX_TIMEOUT_INFINITE));

    static const char* name[MB_COUNT] = {
        "keccakf1600_permute", "poly_ntt", "poly_invntt_tomont", "poly_rej_uniform"
    };
    int errors = 0;
    std::printf("%-22s %14s %12s\n", "primitive", "total_cycles", "per_call");
    for (int i = 0; i < MB_COUNT; ++i) {
        std::printf("%-22s %14llu %12.1f\n", name[i],
                    (unsigned long long)h_cycles[i],
                    (double)h_cycles[i] / iters);
        if (h_cycles[i] == 0) { std::printf("*** %s measured zero\n", name[i]); ++errors; }
    }

    vx_event_release(read_ev); vx_event_release(launch_ev);
    vx_buffer_release(cyc_buf); vx_buffer_release(scr_buf);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);

    if (errors) { std::cout << "FAILED!" << std::endl; return 1; }
    std::cout << "PASSED!" << std::endl;
    return 0;
}
