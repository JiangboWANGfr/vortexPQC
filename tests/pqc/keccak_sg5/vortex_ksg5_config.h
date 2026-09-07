#ifndef VORTEX_KSG5_CONFIG_H
#define VORTEX_KSG5_CONFIG_H

// Minimal mlkem-native configuration: this test uses keccakf1600.c and nothing
// else, so it deliberately does NOT pull in mlk_vortex_alloc.h the way
// tests/pqc/mlkem does. That arena is sized MLK_ARENA_BYTES per hart, and at
// W = 16 with 8 warps it would be 128 harts x 24,576 B = 3,147,264 B of .bss for
// an allocator no Keccak permutation ever calls.

#define MLK_CONFIG_PARAMETER_SET 768
#define MLK_CONFIG_NAMESPACE_PREFIX mlkem
#define MLK_CONFIG_NO_RANDOMIZED_API

#endif
