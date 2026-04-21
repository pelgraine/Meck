"""
patch_nrf52_bsp.py — Pre-build BSP patches for nRF52 Meck builds

Patches the Adafruit nRF52 BSP's LittleFS File class to add a default
constructor. BSP 1.7.0 removed the default File() constructor, but the
Meck screen headers (NotesScreen, TextReaderScreen, EpubZipReader) have
File member variables that need default construction.

Runs automatically before each build via extra_scripts in platformio.ini.
Idempotent — safe to run repeatedly.
"""
Import("env")
import os

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
lfs_src = os.path.join(framework_dir, "libraries", "Adafruit_LittleFS", "src")

# -------------------------------------------------------------------------
# 1. Patch header: add File() default constructor declaration
# -------------------------------------------------------------------------
header_path = os.path.join(lfs_src, "Adafruit_LittleFS_File.h")

with open(header_path, "r") as f:
    h = f.read()

if "File ();" not in h and "File();" not in h:
    h = h.replace(
        "File (Adafruit_LittleFS &fs);",
        "File (Adafruit_LittleFS &fs);\n    File ();  // Meck nRF52 compat"
    )
    with open(header_path, "w") as f:
        f.write(h)
    print("LittleFS File patch: Added default constructor declaration")
else:
    print("LittleFS File patch: OK - header already patched")

# -------------------------------------------------------------------------
# 2. Patch source: add File() default constructor implementation
#    Uses C++11 delegating constructor → File(InternalFS)
# -------------------------------------------------------------------------
source_path = os.path.join(lfs_src, "Adafruit_LittleFS_File.cpp")

with open(source_path, "r") as f:
    s = f.read()

if "File::File()" not in s:
    # Locate InternalFileSystem header (Adafruit BSP has a known typo: "Sytem")
    ifs_include = None
    for dirname in ["InternalFileSytem", "InternalFileSystem"]:
        candidate = os.path.join(framework_dir, "libraries", dirname, "src")
        if os.path.isdir(candidate):
            rel = os.path.relpath(candidate, lfs_src).replace("\\", "/")
            ifs_include = rel + "/InternalFileSystem.h"
            break

    if ifs_include:
        s += "\n// Meck nRF52 compat: default File() constructor\n"
        s += '#include "' + ifs_include + '"\n'
        s += "File::File() : File(InternalFS) {}\n"
        with open(source_path, "w") as f:
            f.write(s)
        print("LittleFS File patch: Added default constructor implementation")
    else:
        print("LittleFS File patch: WARNING - could not find InternalFileSystem")
else:
    print("LittleFS File patch: OK - source already patched")
