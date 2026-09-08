#include "host_reference.h"

// Keep the oracle independent of the device's native hooks and arena.
#undef MLD_CONFIG_FILE
#define MLD_CONFIG_PARAMETER_SET 65
#define MLD_CONFIG_NAMESPACE_PREFIX mldsa_ref
#define MLD_CONFIG_NO_RANDOMIZED_API
extern "C" {
#include "mldsa_native.c"
}

int mldsa_host_reference(const uint8_t* seed, const uint8_t* rnd,
                         const uint8_t* msg, size_t msg_bytes,
                         uint8_t* pk, uint8_t* sk, uint8_t* sig) {
    int rc = mldsa_ref_keypair_internal(pk, sk, seed);
    if (rc != 0) return rc;
    rc = mldsa_ref_signature_internal(sig, msg, msg_bytes, nullptr, 0, rnd, sk, 0);
    if (rc != 0) return rc;
    return mldsa_ref_verify_internal(sig, msg, msg_bytes, nullptr, 0, pk, 0);
}
