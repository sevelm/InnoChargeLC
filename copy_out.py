# copy_out.py  (POST‑Hook, endgültig)
Import("env")
import os, shutil

def copy_bin(target, source, env):            # ← richtige Reihenfolge!
    """
    target[0] = .../MAIN_InnoChargeLC_Fw_<ver>.bin   (fertige BIN)
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


