#ifndef MLDSA_HOST_REFERENCE_H
#define MLDSA_HOST_REFERENCE_H

#include <stddef.h>
#include <stdint.h>

int mldsa_host_reference(const uint8_t* seed, const uint8_t* rnd,
                         const uint8_t* msg, size_t msg_bytes,
                         uint8_t* pk, uint8_t* sk, uint8_t* sig);

#endif
