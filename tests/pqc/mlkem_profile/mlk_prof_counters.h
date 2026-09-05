// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Call counters shared by the two profiling backends and read by the kernel.
// Separate header because both backends and the kernel include it, and the
// backends are pulled in from deep inside the library's own include graph.

#ifndef MLK_PROF_COUNTERS_H
#define MLK_PROF_COUNTERS_H

#include <stdint.h>

enum {
  MLK_PROF_KECCAK_X1 = 0,   // Keccak-f1600, one lane
  MLK_PROF_KECCAK_X4,       // Keccak-f1600, four lanes at once
  MLK_PROF_NTT,
  MLK_PROF_INTT,
  MLK_PROF_REJ_UNIFORM,
  MLK_PROF_MULCACHE,
  MLK_PROF_BASEMUL,
  MLK_PROF_POLY_REDUCE,
  MLK_PROF_COUNT
};

// Device-side only. One translation unit holds the library, both backends and
// the kernel, so a file-scope array is enough -- no linkage, no device-memory
// round trip on the counting path. The host includes this header for the enum
// alone, and a definition there would be an unused variable, which this build
// treats as an error.
#if defined(__VORTEX__)
static uint32_t mlk_prof_counts[MLK_PROF_COUNT];
#endif

#endif /* MLK_PROF_COUNTERS_H */
