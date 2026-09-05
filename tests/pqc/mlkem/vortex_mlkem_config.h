// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// mlkem-native configuration for the Vortex baseline.
//
// Reached through -DMLK_CONFIG_FILE, the library's own override hook, so the
// submodule stays pristine: the accelerated variant differs from this baseline
// by adding MLK_CONFIG_USE_NATIVE_BACKEND_ARITH / _FIPS202 here and nothing
// else, which is what makes the comparison mean anything.

#ifndef VORTEX_MLKEM_CONFIG_H
#define VORTEX_MLKEM_CONFIG_H

// ML-KEM-768: the FIPS 203 parameter set at the security level most deployments
// pick, and the middle of the three for area/latency purposes.
#define MLK_CONFIG_PARAMETER_SET 768

// Public symbols become mlkem_keypair_derand, mlkem_enc_derand, mlkem_dec.
#define MLK_CONFIG_NAMESPACE_PREFIX mlkem

// Drop the randomized API. It is the only thing that needs randombytes(), and
// the derandomized entry points take explicit coins -- which a measurement
// wants anyway, since a run that samples its own randomness is not comparable
// with the next one.
#define MLK_CONFIG_NO_RANDOMIZED_API

#endif // VORTEX_MLKEM_CONFIG_H
