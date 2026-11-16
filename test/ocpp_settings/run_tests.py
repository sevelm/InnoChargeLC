"""Run hardware-free OCPP settings regressions with a native g++ compiler."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", default=shutil.which("g++"))
args = parser.parse_args()
if not args.compiler:
    parser.error("Pass --compiler with the path to a native g++ compiler")

output = root / ".pio" / "ocpp-settings-tests"
output.mkdir(parents=True, exist_ok=True)
source = (root / "src/A_Task_OCPP.cpp").read_text(encoding="utf-8")
patterns = [
    r"struct BackendSettingsV1 \{[\s\S]*?\} backendSettings;",
    r"bool save_backend_settings\([^\n]+\{[\s\S]*?\n\}",
    r"void load_backend_settings\([^\n]+\{[\s\S]*?\n\}",
    r"class BackendConfigurationOperation[^\n]+\{[\s\S]*?\n\};",
]
parts = []
for pattern in patterns:
    match = re.search(pattern, source)
    if not match:
        raise RuntimeError("Cannot locate production code: " + pattern)
    parts.append(match.group(0))
(output / "ocpp_settings_extract.hpp").write_text("\n\n".join(parts), encoding="utf-8")

environment = dict(os.environ)
environment["PATH"] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment["PATH"]
for name in ("test_main", "integration_test"):
    executable = output / (name + (".exe" if os.name == "nt" else ""))
    command = [args.compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror"]
    if os.name == "nt":
        command.append("-static")
    command += ["-Iinclude", "-I.pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src",
                "-I" + str(output), str(Path(__file__).parent / (name + ".cpp")), "-o", str(executable)]
    subprocess.run(command, cwd=root, env=environment, check=True)
    subprocess.run([str(executable)], cwd=root, env=environment, check=True)
