#ifndef _PQC_STACK_H_
#define _PQC_STACK_H_

// Per-hart stack watermark probe, shared by the PQC tests.
//
// The simulator will not catch a stack overflow for you: the runtime disables
// the memory ACL and RAM pages in on demand, so a store past the end of a
// hart's slab just mints a page. With harts 1..N-1 idle, hart 0 can run several
// KB over its slab and every KAT still matches -- the scribble lands in a
// neighbour's unused stack. That is exactly the "instrument silently reads
// nothing" failure this repo already treats as a test failure, so the probe is
// wired to fail the test rather than to print a warning.
//
// Method: paint the hart's slab below the current stack pointer with a magic
// word, run the workload, then scan up from the slab floor for the first word
// that is no longer the magic. The distance from there to the entry sp is the
// peak. If the very first word is dirty the slab filled completely, which means
// the peak is at least the whole slab and the real figure is unknowable from
// inside -- that case reads as "overflowed" and must fail.

// VX_types.h is plain #defines, so the host side can include this header for
// PQC_SLAB_BYTES alone; vx_intrinsics.h is device-only and stays inside the
// guard with the code that needs it.
#include <stdint.h>
#include <VX_types.h>

#define PQC_SLAB_BYTES (1u << VX_MEM_STACK_LOG2_SIZE)
#define PQC_PAINT      0xa5a5a5a5u

// Bytes left unpainted immediately below the caller's sp, so the paint loop
// cannot land on a spill slot of the frame that is running it.
#define PQC_GUARD      64u

#if defined(__VORTEX__)

#include <vx_intrinsics.h>

static inline uint32_t pqc_sp(void) {
  uint32_t v;
  __asm__ volatile("mv %0, sp" : "=r"(v));
  return v;
}

// vx_start.S sets sp = VX_MEM_STACK_BASE_ADDR - (mhartid << STACK_LOG2_SIZE),
// so hart h owns [BASE - (h+1)*SLAB, BASE - h*SLAB). Deriving the floor from
// mhartid rather than by masking sp keeps it correct once sp has moved.
static inline uint32_t pqc_slab_floor(void) {
  return (uint32_t)VX_MEM_STACK_BASE_ADDR
       - (((uint32_t)vx_hart_id() + 1u) << VX_MEM_STACK_LOG2_SIZE);
}

// Paints and returns the paintable span, i.e. the largest peak the scan can
// still distinguish from an overflow.
static inline uint32_t pqc_stack_paint(uint32_t *sp0_out) {
  const uint32_t sp0 = pqc_sp();
  const uint32_t floor_addr = pqc_slab_floor();
  const uint32_t paint_hi = sp0 - PQC_GUARD;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4)
    *(volatile uint32_t*)a = PQC_PAINT;
  *sp0_out = sp0;
  return sp0 - floor_addr;
}

static inline uint32_t pqc_stack_watermark(uint32_t sp0) {
  const uint32_t floor_addr = pqc_slab_floor();
  const uint32_t paint_hi = sp0 - PQC_GUARD;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4)
    if (*(volatile uint32_t*)a != PQC_PAINT)
      return sp0 - a;          // equals the span when the slab filled
  return PQC_GUARD;            // nothing below the guard was touched
}

#endif // __VORTEX__

#endif // _PQC_STACK_H_
