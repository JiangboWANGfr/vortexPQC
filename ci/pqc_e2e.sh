#!/usr/bin/env bash
set -euo pipefail

driver=${1:-rtlsim}
target_args=()
case "$driver" in
    simx|rtlsim) ;;
    avedsim) driver=aved; target_args=(--target=avedsim) ;;
    *) echo "Usage: $0 [simx|rtlsim|avedsim]" >&2; exit 1 ;;
esac

test -f config.mk || { echo "Run from a configured build directory" >&2; exit 1; }
vortex_source=$(sed -n 's/^VORTEX_HOME ?= //p' config.mk)
"$vortex_source/configure"
export CONFIGS="${CONFIGS:+$CONFIGS }-DVX_CFG_EXT_PQC_ENABLE"
export KECCAK=pe
unset ABLATE

for app in mlkem mldsa_profile; do
    make -C "tests/pqc/$app" clean
    make -C "tests/pqc/$app"
    ./ci/blackbox.sh --driver="$driver" "${target_args[@]}" --app="pqc/$app"
done
