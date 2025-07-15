# versioning.py  (PRE‑Hook)
Import("env")
import os, sys

proj_dir = env["PROJECT_DIR"]

# -------- Haupt‑Firmware -------------------------------------------------
ver_file_main = os.path.join(proj_dir, "versionMain.txt")
with open(ver_file_main, encoding="utf-8") as f:
    ver_main = f.read().strip()
    if not ver_main:
        sys.exit("[versioning.py]  versionMain.txt ist leer")

env.Replace(PROGNAME = f"MAIN_IC_Fw_{ver_main}")
env.Append(CPPDEFINES = [("FW_VERSION_MAIN", f"\\\"{ver_main}\\\"")])

# -------- UI / SPIFFS ----------------------------------------------------
ver_file_ui = os.path.join(proj_dir, "versionUi.txt")
with open(ver_file_ui, encoding="utf-8") as f:
    ver_ui = f.read().strip()
    if not ver_ui:
        sys.exit("[versioning.py]  versionUi.txt ist leer")

env.Append(CPPDEFINES = [("FW_VERSION_UI", f"\\\"{ver_ui}\\\"")])