// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Baseline configuration plus the two counting backends.
//
// The baseline config is included rather than restated: this build has to be
// the mlkem test's build in every respect except the counting, or the counts
// belong to something else.

#ifndef VORTEX_PROF_CONFIG_H
#define VORTEX_PROF_CONFIG_H

#include "vortex_mlkem_config.h"

#define MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLK_CONFIG_FIPS202_BACKEND_FILE "mlk_prof_fips202.h"

#define MLK_CONFIG_USE_NATIVE_BACKEND_ARITH
#define MLK_CONFIG_ARITH_BACKEND_FILE "mlk_prof_arith.h"

#endif // VORTEX_PROF_CONFIG_H
