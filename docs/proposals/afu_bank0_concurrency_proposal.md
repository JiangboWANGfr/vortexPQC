# Concurrent CP and core traffic on AFU bank 0

The ML-KEM arithmetic tests exposed an XRT/RTL timing difference that does
not originate in NTT arithmetic. All scalar samples matched, while every
W32 sample added about 4,000 cycles on XRT. A minimal probe localizes the
difference to bank 0: 32 strided lanes take 4,734 cycles on RTL and 13,732
on XRT, whereas moving the same accesses to bank 1 gives 5,113 and 5,082.
Moving a seven-register stack frame by 64 bytes likewise moves the penalty.
Empty BAR, WSYNC, and contiguous accesses agree between backends.

`VX_afu_wrap.bank0_arb` merges core traffic with the command processor's
device-memory port. Its default `MULTI_OUT=0` permits only one outstanding
read and one outstanding write, holding ownership until RLAST/B completes.
The core-only RTL path and SimX permit concurrent requests. Each lane's
stack is 8,192 bytes apart, so a given stack offset maps all 32 lanes to
the same interleaved bank. The shared bank's serialization therefore adds
substantial delay even when the command processor is idle.

An isolated build changing only the arbiter to `MULTI_OUT=1` reduces the
instrumented W32 C poly_reduce interval from 9,904 to 5,896 cycles; core RTL
takes 5,845. The unmodified kernel and host pass in both configurations.
The extra delay spans both the call and the following LSU-draining BAR;
the final WSYNC interval remains 36 cycles in all RTL/XRT variants.

The integration enables the existing multi-outstanding arbiter and
reserve the platform AXI ID's highest bit for the source index. Core AXI
tags must use the remaining bits through the existing tag-width adapter,
with explicit zero extension on requests and truncation after response
routing. CP device IDs retain all six payload bits. The platform ID must
therefore provide at least seven bits. The separate CP host port is unchanged.
The generic arbiter's default and arbitration policy are unchanged.

The new protocol test also reproduced a separate address-stability defect
in the existing multi-outstanding implementation. If source 1 offers an
address while the downstream port is stalled, a later source 0 request can
replace that address before acceptance. `STICKY=1` retains the previous
accepted grant; it does not lock the first stalled offer. Both AR and AW now
lock the offered source until its handshake. This adds no pipeline stage
and leaves arbitration unchanged when there is no stall. The single-outstanding
branch and the global priority arbiter are unchanged. The command processor's
two existing multi-outstanding crossbars also use this corrected logic.

The protocol regression reaches six pending reads before any read response,
and six completed write bursts before any write response. Responses cross
sources and complete IDs out of issue order, including identical low IDs
from different sources. It checks one/two-beat transfers, W-burst ownership,
and payload stability under backpressure on all five channels. The original
test fails before the address lock and passes afterward, with stall coverage
AR/AW/W/R/B = 5/5/24/24/18 cycles. A separate `MULTI_OUT=0` negative control
blocks on its second read while responses are withheld. The test does not
exercise multiple outstanding transactions sharing one complete ID or
simultaneous read and write phases. The `hw-mm-axi-arb` CI case executes it.

Final RTL lint and elaboration cover platform IDs of 32 and 7 bits, normal
and debug tags, and merged and separate memory banks: all eight combinations
pass without new width warnings. Six-bit platform IDs are rejected. Debug
builds preserve all 53 internal tag bits through two 16-entry tag buffers;
the source bit cannot overwrite them. Seven-bit platform IDs also use the
tag buffers in normal mode. This is elaboration coverage; runtime XRT uses
the normal 32-bit platform IDs. Evidence includes the elaborated tag-buffer
widths, bank-0 `MULTI_OUT=1`, address-lock registers, and source hashes in
`build32_ntt_axi_locked/id_validation/summary.json`.

Core RTL and SimX timing behavior is unchanged. These changes concern the
AFU/CP AXI path outside the core-only timing model, so the existing parity
gates remain applicable without adding AFU serialization to SimX.

The final runtime passes the full CP unit (NOP retirement, completion write,
and queue reset), three CP AXI-path scenarios, seven AFU reset/drain scenarios,
and three device reopen iterations in each of cold and warm mode. A separate
XRT probe with `VX_MEM_RSP_REORDER=1 VX_MEM_RSP_STALL=4` completes with 365
responses actually reordered, zero mismatches, 4,232 instructions, and
123,473 cycles. Those stressed cycles are not used as a performance denominator.
The earlier ID-only runtime's 453-reorder result is archived separately.

Final bank-placement probe medians use the same kernel and 4,232 retired
instructions on all three backends; each row has one warmup and three samples:

| W32 interval | Core RTL | Original XRT | Final XRT |
| --- | ---: | ---: | ---: |
| Empty | 132 | 132 | 132 |
| BAR | 188 | 188 | 188 |
| WSYNC | 140 | 140 | 140 |
| Contiguous seven-word accesses | 191 | 191 | 191 |
| Strided global accesses, bank 0 | 4,734 | 13,732 | 5,187 |
| Same global accesses +64 bytes, bank 1 | 5,113 | 5,082 | 5,082 |
| Seven-register stack, bank 0 | 3,434 | 13,347 | 3,421 |
| Seven-register stack, bank 1 | 4,843 | 4,987 | 4,987 |

Short memory-sensitive intervals retain some AFU/DRAM scheduling differences;
the global bank-0 row is still 9.6% above core RTL. Removing the serialization
does not make every individual memory interval identical across platforms.

The full `mlkem_arith_xn` test now takes 20,609,521 XRT cycles against
20,609,824 on core RTL, with exactly 1,646,854 retired instructions in both.
The difference is -303 cycles (-0.001470%), compared with the original
23,952,861 XRT cycles (+16.220599%). All 72 timed samples pass, including
10,240 cooperative coefficient comparisons and mulcache guards; all 65,536
signed16 reduction inputs also pass the independent host oracle.
The 24 scalar intervals remain identical to RTL. Across the 48 W32 intervals,
XRT minus RTL has median +3.5 cycles and range -172 to +153, compared with
the original median +3,947.5 and range +3,856 to +4,114.

Final complete ML-KEM-768 KAT runs pass all 26 requests across the following
five runs. Each software arm reuses its original host and kernel binaries;
only the XRT runtime changes. Batch makespans exclude host/launch overhead:

| Arithmetic arm | Final XRT M1 cycles | Final XRT M8 cycles |
| --- | ---: | ---: |
| NTT-only denominator | 7,711,416 | 10,482,785 |
| mulcache + basemul + reduce, ISE | 5,999,744 | 8,992,026 |
| basemul + reduce, C | not run | 8,874,609 |

Relative to these matching denominators, all-three ISE reduces M1 cycles
by 22.197% (1.285x), and basemul+reduce C reduces M8 cycles by 15.341%
(1.181x). The all-three M8 reduction is 14.221%. The AFU correction itself
changes these KEM makespans by at most 0.35%; its main effect is removing the bank bias
in the independent primitive test. In particular, the earlier M8 reduction
of 15.686% belongs to the old AFU's 10,518,691-cycle denominator and must
not be reused with these new measurements.

Experiments and exact binaries are under `build32_nttmul/arith_phase/`,
`build32_nttmul/arith_timing_probe/`, and `build32_ntt_axi_multiout/evidence/`.
Results from the old and new AFU must retain separate labels. Any new KEM
speedup comparison must rerun its denominator with the same AFU.

Data: [AFU timing comparisons](../../pqc/results/afu_bank0_comparison.csv)
and [all direct arithmetic samples](../../pqc/results/mlkem_arith_afu_primitives.csv).

The current source and final runtime evidence are archived under
`build32_ntt_axi_locked/evidence/`, including build provenance, unchanged
kernel hashes, protocol regressions, integration logs, and the source patch.
`build32_ntt_axi_fixed/evidence/` records the intermediate ID-only runtime
before the address lock. The previous Vivado V80/250 MHz core reports do
not measure this AFU change; no new AFU implementation or power estimate
is claimed.
