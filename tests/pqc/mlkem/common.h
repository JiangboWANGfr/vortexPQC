#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <mlkem_native.h>

// Sizes come from the library rather than being restated here: a second copy
// of 1184/2400/1088 is a second thing to get wrong when the parameter set
// changes.
#define MLKEM_PK_BYTES  MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET)
#define MLKEM_SK_BYTES  MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET)
#define MLKEM_CT_BYTES  MLKEM_CIPHERTEXTBYTES(MLK_CONFIG_PARAMETER_SET)
#define MLKEM_SS_BYTES  MLKEM_BYTES
#define MLKEM_SYM_BYTES MLKEM_SYMBYTES

// status[] slots, so a failure names the step that produced it.
#define MLKEM_ST_KEYPAIR 0
#define MLKEM_ST_ENCAPS  1
#define MLKEM_ST_DECAPS  2
#define MLKEM_ST_COUNT   3

// Cycle deltas per phase, read on the device with vx_rdcycle(). Phase-level is
// as fine as this gets without touching the library; attributing time to Keccak
// versus the NTT comes later, through the library's own backend hooks, which
// can be wrapped without modifying it.
#define MLKEM_CY_KEYPAIR 0
#define MLKEM_CY_ENCAPS  1
#define MLKEM_CY_DECAPS  2
#define MLKEM_CY_COUNT   3

// Every buffer lives in device memory. The key and ciphertext material alone
// is ~4.7 KB against an 8 KB per-thread stack (VX_MEM_STACK_LOG2_SIZE = 13),
// which the library's own working set then has to fit inside -- so none of it
// goes on the stack.
typedef struct {
  uint64_t coins_kp_addr;   // in  : 2 * MLKEM_SYM_BYTES (d || z)
  uint64_t coins_enc_addr;  // in  : MLKEM_SYM_BYTES
  uint64_t pk_addr;         // out : MLKEM_PK_BYTES
  uint64_t sk_addr;         // out : MLKEM_SK_BYTES
  uint64_t ct_addr;         // out : MLKEM_CT_BYTES
  uint64_t ss_enc_addr;     // out : MLKEM_SS_BYTES
  uint64_t ss_dec_addr;     // out : MLKEM_SS_BYTES
  uint64_t status_addr;     // out : MLKEM_ST_COUNT * int32_t
  uint64_t cycles_addr;     // out : MLKEM_CY_COUNT * uint64_t
} kernel_arg_t;

#endif
