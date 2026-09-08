import pathlib
import resource
import subprocess
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
binary = sys.argv[1]
for mode, name in enumerate(("progress", "stopped_fu", "unrelated_fu", "dependency")):
    result = subprocess.run([binary, f"+mode={mode}"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pathlib.Path(f"watchdog-{name}.log").write_text(result.stdout)
    if mode == 0:
        assert result.returncode == 0 and "PASS:" in result.stdout, result.stdout
    else:
        assert result.returncode != 0 and "VX_scoreboard.sv" in result.stdout, result.stdout
        assert "timeout: wid=" in result.stdout, result.stdout
        if mode == 3:
            assert "timeout: wid=0," in result.stdout, result.stdout
    print(f"PASS: watchdog {name}")
