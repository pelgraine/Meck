"""
PlatformIO pre-build script for the envs built with pioarduino and
custom_sdkconfig (the T-Deck MAX). Works around two problems of that setup.

1. pioarduino builds such an env in two passes: first it rebuilds the Arduino
   ESP-IDF libraries around a dummy sketch in .dummy/ (PROJECT_SRC_DIR points
   there and lib_deps are cleared), then a child `pio run` builds the
   firmware. build_src_filter still applies in the first pass, and its
   "../examples" and "../variants" entries reach the real sources from .dummy/
   too, which then fail without their libraries. Keep only the dummy sketch in
   that pass.

2. The child `pio run` is the pioarduino core in the platform's own Python
   environment (penv). A fresh penv gets pioarduino 6.2.0.post1, which reads
   its own version as the pre-release 6.2.0-post.1 and so rejects the platform
   (it needs >= 6.2.0) and uninstalls it. Pin the release it is a post of.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

Import("env")


def only_dummy_sketch(env, node):
    src_dir = Path(env.subst("$PROJECT_SRC_DIR")).resolve()
    if src_dir.name != ".dummy":
        return node
    try:
        Path(node.srcnode().get_abspath()).resolve().relative_to(src_dir)
    except ValueError:
        return None
    return node


env.AddBuildMiddleware(only_dummy_sketch)


def pin_penv_core():
    penv = Path(env.subst("$PROJECT_CORE_DIR")) / "penv"
    bindir = penv / ("Scripts" if sys.platform == "win32" else "bin")
    exe = ".exe" if sys.platform == "win32" else ""
    python, uv = bindir / ("python" + exe), bindir / ("uv" + exe)
    if not python.exists() or not uv.exists():
        return
    try:
        out = subprocess.run([str(python), "-c", "import platformio; print(platformio.__version__)"],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return
    m = re.match(r"^(\d+\.\d+\.\d+)\.post\d+$", out.stdout.strip())
    if not m:
        return
    print("pioarduino_fixes: penv core %s rejects the platform, pinning pioarduino==%s"
          % (out.stdout.strip(), m.group(1)))
    subprocess.run([str(uv), "pip", "install", "--quiet", "--python", str(python),
                    "pioarduino==" + m.group(1)])


pin_penv_core()
