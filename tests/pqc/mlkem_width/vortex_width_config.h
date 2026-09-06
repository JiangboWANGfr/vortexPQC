// Baseline mlkem config plus the SIMT FIPS-202 backend.
//
// Everything except the Keccak backend is the mlkem test's build, included
// rather than restated, so the only thing the width sweep varies is how many
// lanes a Keccak batch lands on.
#ifndef VORTEX_WIDTH_CONFIG_H
#define VORTEX_WIDTH_CONFIG_H

#include "vortex_mlkem_config.h"

#if defined(PQC_SERIAL_FIPS202_ONLY)
// The library's own "no batched Keccak here" switch: gen_matrix samples all
// nine entries one at a time and the noise generators drop the padding lane.
// This is the honest W=1 arm -- the one the algorithmic-independence model
// describes -- as opposed to the default build, which still pays for a fourth
// lane it throws away.
#define MLK_CONFIG_SERIAL_FIPS202_ONLY
#endif

#define MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLK_CONFIG_FIPS202_BACKEND_FILE "mlk_simt_fips202.h"

// The per-hart arena now comes from vortex_mlkem_config.h, which every ML-KEM
// test shares -- two arenas with different hart-index conventions is how the
// aliasing bug comes back. The PQC_ARENA switch is gone with it: without the
// arena a second lane cannot run at all, so it was never a real option here.

#endif
