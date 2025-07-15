# versioning.py  (PRE-Hook, korrigiert)
Import("env")
import os, sys

ver_file = os.path.join(env["PROJECT_DIR"], "versionMain.txt")

with open(ver_file, "r", encoding="utf-8") as f:
    ver = f.read().strip()
    if not ver:
        sys.exit("[versioning.py]  versionMain.txt ist leer")

# → Firmware-Dateiname
env.Replace(PROGNAME = f"MAIN_IC_Fw_{ver}")

# → Makro   -DFW_VERSION=\"V.2025.07.14-001\"
env.Append(CPPDEFINES = [("FW_VERSION_MAIN", f"\\\"{ver}\\\"")])
