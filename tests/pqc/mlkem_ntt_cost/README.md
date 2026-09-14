# ML-KEM NTT arithmetic sensitivity

This probe compares a real Montgomery `fqmul` with an opaque identity in the
same single-lane forward and inverse butterfly loops. The real arms call the
upstream arithmetic and use its twiddle table. Every sampled input, including
warmup, is checked coefficient by coefficient against upstream C outside the
timed region. This is a transform reference check, not a full ML-KEM KAT.
Unrolling is disabled in both arms to preserve matching loop structure: this
controlled baseline is not the production library's optimized cycle count.

The identity emits two dependent XOR instructions and returns its first
operand. Both inputs remain live, so the compiler must retain coefficient and
twiddle loads. The result is opaque to the optimizer, and every output feeds a
printed checksum. Identity outputs are intentionally invalid cryptographic
results. Inverse Barrett reductions remain real; only `fqmul` is replaced,
including its 256 normalization calls. Forward/inverse calls contain
896/1152 `fqmul` operations, respectively.

All arms use the same global polynomial storage. Input initialization and
reference checks are excluded from timing; completion fences and counter
overhead are included. One pair is discarded as warmup, then arm order
alternates each iteration. The printed instruction count is the 32-bit retired
instruction delta per transform, accumulated into 64 bits; each transform must
retire fewer than 2^32 instructions. The launch has exactly one active lane.

From a configured build directory, reconfigure after adding this directory,
then build and run with matching hardware macros:

```sh
../configure --xlen=32 --tooldir=/path/to/toolchains
CONFIGS="-DVX_CFG_EXT_NTT_ENABLE" make -C tests/pqc/mlkem_ntt_cost
CONFIGS="-DVX_CFG_EXT_NTT_ENABLE" ./ci/blackbox.sh --driver=simx \
  --app=pqc/mlkem_ntt_cost --args="-n8"
```

`cycle_sensitivity` is `(real - identity) / real`. It estimates sensitivity to
removing multiply/Montgomery arithmetic in this kernel and mapping. It is not
an exact additive cost breakdown, an end-to-end acceleration, or the speedup
of a proposed instruction: instruction scheduling, register allocation and
data values also change. Residual cycles include loop/address work, coefficient
and twiddle accesses, butterfly add/subtract, two XORs per replaced multiply,
and inverse Barrett reduction. Inspect `kernel.dump` to confirm the inner
loops still contain coefficient loads/stores, twiddle loads and back edges
before interpreting a new compiler's results.
