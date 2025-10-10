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

# Stattdessen: Version in SPIFFS-Datei für die Web-UI ablegen
data_dir = os.path.join(proj_dir, "data")
os.makedirs(data_dir, exist_ok=True)
ui_ver_path = os.path.join(data_dir, "ui_version.txt")
with open(ui_ver_path, "w", encoding="utf-8") as f:
    f.write(ver_ui + "\n")
