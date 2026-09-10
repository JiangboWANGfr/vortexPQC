import resource
import subprocess
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for mode, diagnostic in (
    ("bad_mask", "KROUND25 requires a full warp mask"),
    ("bad_round", "KROUND25 requires a round in 0..23"),
):
    result = subprocess.run(
        [sys.argv[1], "+" + mode], capture_output=True, text=True, timeout=10
    )
    assert result.returncode != 0 and diagnostic in result.stdout + result.stderr, (
        mode, result.returncode, result.stdout, result.stderr
    )
print("PASSED! partial mask and invalid round diagnosed")
