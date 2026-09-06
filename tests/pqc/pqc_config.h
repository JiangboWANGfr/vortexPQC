#ifndef _PQC_CONFIG_H_
#define _PQC_CONFIG_H_

// Make every run say what machine it actually ran on.
//
// This exists because of a real error. The ML-KEM M x L grid was swept through
// ci/blackbox.sh --warps=8; the ML-DSA one was swept through `make run-simx`,
// which does not carry that flag, so it ran at the default four warps. Two
// tables that looked directly comparable were taken on different machines, and
// M=8 running two waves on four warp slots read as a memory wall at M=4. It was
// caught only because the instruction count was exactly 2.0000x with IPC
// unchanged -- a capacity wall depresses IPC, a second wave does not -- and
// because enabling L2 moved that cell by 0.02%. Nothing in the harness would
// have caught it, and a wrong architectural conclusion was one step away.
//
// The fix is not discipline, it is a printed line. Every PQC test emits one
// CONFIG: record, and every sweep script copies it into its results file, so a
// number can never be separated from the machine that produced it.
//
// The values come from vx_device_query rather than from the VX_CFG_* macros
// wherever the device can answer: the macros say what the host was compiled to
// believe, the device says what is actually there. That distinction is the
// whole point. The two cache-hierarchy flags have no capability ID, so they are
// read from the macros and marked as such.
//
// Borrowed from tests/crypto in the vortexCrypto tree, which solved this first.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vortex2.h>

namespace pqc {

struct config {
  uint64_t clusters, cores, warps, threads;
  uint64_t lmem_bytes, line_bytes, isa_flags;
  int l2, l3;
  const char* driver;
};

inline config query_config(vx_device_h dev) {
  config c{};
  auto q = [&](uint32_t id, uint64_t* out) {
    if (vx_device_query(dev, id, out) != VX_SUCCESS) *out = 0;
  };
  q(VX_CAPS_NUM_CLUSTERS,     &c.clusters);
  q(VX_CAPS_NUM_CORES,        &c.cores);
  q(VX_CAPS_NUM_WARPS,        &c.warps);
  q(VX_CAPS_NUM_THREADS,      &c.threads);
  q(VX_CAPS_LOCAL_MEM_SIZE,   &c.lmem_bytes);
  q(VX_CAPS_CACHE_LINE_SIZE,  &c.line_bytes);
  q(VX_CAPS_ISA_FLAGS,        &c.isa_flags);
  // No capability ID exposes the L2/L3 enables, so these are the host's
  // compile-time view. They agree with the device because both come from the
  // same CONFIGS in one make invocation -- but they are the one part of this
  // line that is asserted rather than observed.
#if defined(VX_CFG_L2_ENABLED)
  c.l2 = VX_CFG_L2_ENABLED;
#else
  c.l2 = -1;
#endif
#if defined(VX_CFG_L3_ENABLED)
  c.l3 = VX_CFG_L3_ENABLED;
#else
  c.l3 = -1;
#endif
  const char* d = std::getenv("VORTEX_DRIVER");
  c.driver = (d && *d) ? d : "unknown";
  return c;
}

// One line, greppable, in a fixed field order. `requests` and `lanes` are the
// launch shape the test chose; pass 0 for either if it does not apply.
inline config print_config(vx_device_h dev, uint32_t requests, uint32_t lanes) {
  const config c = query_config(dev);
  std::printf("CONFIG: driver=%s clusters=%llu cores=%llu warps=%llu threads=%llu "
              "M=%u L=%u l2=%d l3=%d lmem=%lluB line=%lluB isa=0x%llx\n",
              c.driver,
              (unsigned long long)c.clusters, (unsigned long long)c.cores,
              (unsigned long long)c.warps,    (unsigned long long)c.threads,
              requests, lanes, c.l2, c.l3,
              (unsigned long long)c.lmem_bytes, (unsigned long long)c.line_bytes,
              (unsigned long long)c.isa_flags);
  std::fflush(stdout);
  return c;
}

// Refuse a launch the machine cannot host, rather than running a different
// experiment than the one asked for. M requests need M warp slots to be
// concurrent; beyond that they run in waves, which is a legitimate thing to
// measure but never the thing someone sweeping an M axis meant to measure.
// Returns 0 to proceed, non-zero to exit with that code.
inline int require_slots(const config& c, uint32_t requests, uint32_t lanes) {
  int bad = 0;
  if (requests > c.warps) {
    std::printf("*** -b %u needs %u warp slots, device has %llu: the extra requests\n"
                "    would run in waves and the M axis would measure the waves.\n"
                "    Rebuild with CONFIGS=\"-DVX_CFG_NUM_WARPS=%u\" (or blackbox --warps=%u).\n",
                requests, requests, (unsigned long long)c.warps, requests, requests);
    ++bad;
  }
  if (lanes > c.threads) {
    std::printf("*** -t %u needs %u lanes, device has %llu per warp.\n"
                "    Rebuild with CONFIGS=\"-DVX_CFG_NUM_THREADS=%u\".\n",
                lanes, lanes, (unsigned long long)c.threads, lanes);
    ++bad;
  }
  return bad;
}

} // namespace pqc

#endif // _PQC_CONFIG_H_
