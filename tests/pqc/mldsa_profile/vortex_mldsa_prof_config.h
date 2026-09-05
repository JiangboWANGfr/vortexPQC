// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// The ML-DSA baseline configuration plus the two counting backends. The
// baseline is included rather than restated, so this build differs from the
// measured one only by the counting.

#ifndef VORTEX_MLDSA_PROF_CONFIG_H
#define VORTEX_MLDSA_PROF_CONFIG_H

#include "vortex_mldsa_config.h"

#define MLD_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLD_CONFIG_FIPS202_BACKEND_FILE "mld_prof_fips202.h"

#define MLD_CONFIG_USE_NATIVE_BACKEND_ARITH
#define MLD_CONFIG_ARITH_BACKEND_FILE "mld_prof_arith.h"

#endif // VORTEX_MLDSA_PROF_CONFIG_H
