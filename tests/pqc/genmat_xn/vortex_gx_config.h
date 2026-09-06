// The mlkem baseline config, unchanged. The xN batching in this test sits
// above the library, in the caller, not inside it -- which is the point: the
// x4 API is not a backend hook, so a wider batch cannot be reached through
// one.
#ifndef VORTEX_GX_CONFIG_H
#define VORTEX_GX_CONFIG_H
#include "vortex_mlkem_config.h"
#endif
