#include <vortex2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "pqc_config.h"

// The host reference is the library's own permutation, compiled for the host.
// SRCS and VX_SRCS are separate binaries, so including the .c here does not
// collide with the kernel's copy; tests/pqc/genmat_xn uses the same pattern.
extern "C" {
#include "src/common.h"
#include "src/fips202/keccakf1600.c"
}

#define CHECK(_e) do { int _r = (_e); if (_r) { \
  std::fprintf(stderr, "FAIL %s:%d: '%s' -> %d\n", __FILE__, __LINE__, #_e, _r); \
  std::exit(-1); } } while (0)

static inline uint64_t ks_seed(unsigned state, unsigned word) {
  return 0x0123456789abcdefULL * (uint64_t)(state + 1) +
         0x9e3779b97f4a7c15ULL * (uint64_t)word;
}

int main(int argc, char** argv) {
    const char* kf = "kernel.vxbin";
    uint32_t lanes = 16, perms = 4, fences = 2, arm = KS_ARM_SG5;
    int c;
    while ((c = getopt(argc, argv, "k:t:p:f:a:h")) != -1) {
        if (c == 'k') kf = optarg;
        else if (c == 't') lanes  = (uint32_t)std::atoi(optarg);
        else if (c == 'p') perms  = (uint32_t)std::atoi(optarg);
        else if (c == 'f') fences = (uint32_t)std::atoi(optarg);
        else if (c == 'a') arm    = (std::strcmp(optarg,"sg1")==0) ? KS_ARM_SG1 : KS_ARM_SG5;
        else { std::cout << "Usage: [-k kernel] [-t lanes] [-p perms] "
                            "[-f fences 0|1|2] [-a sg1|sg5]\n";
               std::exit(c == 'h' ? 0 : -1); }
    }

    // Validate the host reference itself before trusting it as the oracle:
    // Keccak-f1600 on an all-zero state gives A[0][0] = f1258f7940e1dde7.
    {
        uint64_t z[KS_WORDS]; std::memset(z, 0, sizeof(z));
        mlk_keccakf1600_permute(z);
        if (z[0] != 0xf1258f7940e1dde7ULL) {
            std::printf("*** host reference fails the FIPS 202 all-zero KAT: "
                        "A[0][0] = %016llx\n", (unsigned long long)z[0]);
            return -1;
        }
    }

    if (arm == KS_ARM_SG5 && lanes < KS_LANES_PER_STATE) {
        std::printf("*** SG5 needs at least %d lanes, -t is %u\n",
                    KS_LANES_PER_STATE, lanes);
        return -1;
    }
    const uint32_t nstates = (arm == KS_ARM_SG1) ? lanes : lanes / KS_LANES_PER_STATE;
    if (perms < 1) { std::printf("*** -p must be >= 1\n"); return -1; }

    vx_device_h dev=nullptr; CHECK(vx_device_open(0,&dev));
    const pqc::config cfg = pqc::print_config(dev, 1, lanes);
    if (pqc::require_slots(cfg, 1, lanes) != 0) { vx_device_release(dev); return -1; }
    vx_queue_info_t qi={sizeof(qi),nullptr,VX_QUEUE_PRIORITY_NORMAL,0};
    vx_queue_h q=nullptr; CHECK(vx_queue_create(dev,&qi,&q));

    const size_t sbytes = (size_t)nstates * KS_WORDS * sizeof(uint64_t);
    vx_buffer_h sb=nullptr, cb=nullptr;
    CHECK(vx_buffer_create(dev, sbytes, VX_MEM_WRITE, &sb));
    CHECK(vx_buffer_create(dev, KS_CY_COUNT*sizeof(uint64_t), VX_MEM_WRITE, &cb));

    kernel_arg_t arg{};
    CHECK(vx_buffer_address(sb,&arg.states_addr));
    CHECK(vx_buffer_address(cb,&arg.cycles_addr));
    arg.arm = arm; arg.lanes = lanes; arg.perms = perms;
    arg.fences = fences; arg.nstates = nstates;

    // Poison both buffers: a kernel that never ran must fail, not read back
    // zeros that could be mistaken for a result.
    std::vector<uint64_t> poison(nstates * KS_WORDS, 0xDEADBEEFDEADBEEFULL);
    CHECK(vx_enqueue_write(q, sb, 0, poison.data(), sbytes, 0, nullptr, nullptr));
    std::vector<uint64_t> cpoison(KS_CY_COUNT, 0xDEADBEEFDEADBEEFULL);
    CHECK(vx_enqueue_write(q, cb, 0, cpoison.data(), KS_CY_COUNT*8, 0, nullptr, nullptr));

    vx_module_h mod=nullptr; vx_kernel_h kern=nullptr;
    CHECK(vx_module_load_file(dev,kf,&mod));
    CHECK(vx_module_get_kernel(mod,"main",&kern));

    vx_launch_info_t li{}; li.struct_size=sizeof(li); li.kernel=kern;
    li.args_host=&arg; li.args_size=sizeof(arg);
    li.ndim=1; li.grid_dim[0]=1; li.block_dim[0]=lanes;
    vx_event_h lev=nullptr,e=nullptr;
    CHECK(vx_enqueue_launch(q,&li,0,nullptr,&lev));

    std::vector<uint64_t> h_out(nstates * KS_WORDS), cyc(KS_CY_COUNT,0);
    CHECK(vx_enqueue_read(q,h_out.data(),sb,0,sbytes,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e); e=nullptr;
    CHECK(vx_enqueue_read(q,cyc.data(),cb,0,cyc.size()*8,1,&lev,&e));
    CHECK(vx_event_wait_value(e,1,VX_TIMEOUT_INFINITE)); vx_event_release(e);

    int errors = 0;
    if (cyc[KS_CY_RUN] == 0xDEADBEEFDEADBEEFULL) {
        std::printf("*** cycle counter untouched -- the kernel never ran\n"); ++errors;
    }

    // Every state, every word, against the library. A lane-mapping error shows
    // up here and not as a speedup.
    uint32_t bad_states = 0, bad_words = 0;
    for (uint32_t s = 0; s < nstates; ++s) {
        uint64_t ref[KS_WORDS];
        for (unsigned j = 0; j < KS_WORDS; ++j) ref[j] = ks_seed(s, j);
        for (uint32_t p = 0; p < perms; ++p) mlk_keccakf1600_permute(ref);
        uint32_t bw = 0;
        for (unsigned j = 0; j < KS_WORDS; ++j)
            if (h_out[(size_t)s*KS_WORDS + j] != ref[j]) ++bw;
        if (bw) {
            if (bad_states < 2)
                for (unsigned j = 0, shown = 0; j < KS_WORDS && shown < 3; ++j)
                    if (h_out[(size_t)s*KS_WORDS + j] != ref[j]) {
                        std::printf("    state %u word %2u (x=%u,y=%u): got %016llx  want %016llx\n",
                                    s, j, j%5, j/5,
                                    (unsigned long long)h_out[(size_t)s*KS_WORDS + j],
                                    (unsigned long long)ref[j]);
                        ++shown;
                    }
            ++bad_states; bad_words += bw;
        }
    }
    if (bad_states) {
        std::printf("*** %u of %u states wrong (%u of %u words)\n",
                    bad_states, nstates, bad_words, nstates*KS_WORDS);
        ++errors;
    }
    if (cyc[KS_CY_STACK] != 0xDEADBEEFDEADBEEFULL &&
        cyc[KS_CY_STACK] >= cyc[KS_CY_SPAN]) {
        std::printf("*** stack peak %llu B filled its whole %llu B paintable slab\n",
                    (unsigned long long)cyc[KS_CY_STACK],
                    (unsigned long long)cyc[KS_CY_SPAN]); ++errors;
    }

    const uint64_t total = (uint64_t)nstates * perms;
    std::printf("KECCAK arm=%s W=%u states=%u perms=%u fences=%u | "
                "cycles=%llu total_perms=%llu cy_per_perm=%.1f | stack=%llu/%llu | bad=%u\n",
                arm==KS_ARM_SG1?"sg1":"sg5", lanes, nstates, perms, fences,
                (unsigned long long)cyc[KS_CY_RUN], (unsigned long long)total,
                total ? (double)cyc[KS_CY_RUN]/(double)total : 0.0,
                (unsigned long long)cyc[KS_CY_STACK], (unsigned long long)cyc[KS_CY_SPAN],
                bad_words);
    vx_device_dump_perf(dev, stdout);

    vx_event_release(lev);
    vx_buffer_release(sb); vx_buffer_release(cb);
    vx_kernel_release(kern); vx_module_release(mod);
    vx_queue_release(q); vx_device_release(dev);
    std::cout << (errors ? "FAILED!" : "PASSED!") << std::endl;
    return errors ? -1 : 0;
}
