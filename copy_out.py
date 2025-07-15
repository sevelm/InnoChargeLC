# copy_out.py  (POST‑Hook, endgültig)
Import("env")
import os, shutil

def copy_bin(target, source, env):            # ← richtige Reihenfolge!
    """
    target[0] = .../MAIN_IC_Fw_<ver>.bin   (fertige BIN)
    Kopiert sie nach <PROJECT_DIR>/build/
    """
    bin_src = str(target[0])                  # garantiert .bin
    dst_dir = os.path.join(env["PROJECT_DIR"], "build")
    os.makedirs(dst_dir, exist_ok=True)

    dst = os.path.join(dst_dir, os.path.basename(bin_src))
    shutil.copy2(bin_src, dst)
    print("[copy_out.py]  Firmware kopiert nach:", dst)

# Hook an die fertige BIN hängen
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin)

# ------------------------------------------------------------------------
# --- neu: SPIFFS‑Image in UI_IC_Fw_<ver_ui>.bin umkopieren --------------
# ------------------------------------------------------------------------
proj_dir = env["PROJECT_DIR"]
ver_ui_file = os.path.join(proj_dir, "versionUi.txt")

try:
    with open(ver_ui_file, encoding="utf-8") as f:
        ver_ui = f.read().strip()
        if not ver_ui:
            raise ValueError("versionUi.txt ist leer")
except Exception as e:
    print("[copy_out.py]  SPIFFS‑Hook deaktiviert:", e)
else:
    def copy_spiffs(target, source, env):
        """
        target[0] = .../spiffs.bin     (vom buildfs‑Target)
        Kopiert sie nach build/UI_IC_Fw_<ver_ui>.bin
        """
        src_img = str(target[0])
        dst_dir = os.path.join(proj_dir, "build")
        os.makedirs(dst_dir, exist_ok=True)

        dst_img = os.path.join(dst_dir, f"UI_IC_Fw_{ver_ui}.bin")
        shutil.copy2(src_img, dst_img)
        print("[copy_out.py]  SPIFFS kopiert nach:", dst_img)

    env.AddPostAction("$BUILD_DIR/spiffs.bin", copy_spiffs)   # Hook aktiv



