"""
PlatformIO post-build script: merge bootloader + partitions + firmware
into a single flashable binary.

Output: .pio/build/<env>/firmware_merged.bin
Flash:  esptool.py --chip esp32s3 write_flash 0x0 firmware_merged.bin

Place this file in the project root alongside platformio.ini.
Add to each environment (or the base section):
  extra_scripts = post:merge_firmware.py
"""

Import("env")

def merge_bin(source, target, env):
    import subprocess, os

    build_dir = env.subst("$BUILD_DIR")
    env_name = env.subst("$PIOENV")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")
    output     = os.path.join(build_dir, "firmware_merged.bin")

    # Verify all inputs exist
    for f in [bootloader, partitions, firmware]:
        if not os.path.isfile(f):
            print(f"[merge] WARNING: {f} not found, skipping merge")
            return

    # Read flash settings from board config
    flash_mode = env.BoardConfig().get("build.flash_mode", "qio")
    flash_freq = env.BoardConfig().get("build.f_flash", "80000000L").rstrip("L")
    flash_size = env.BoardConfig().get("upload.flash_size", "16MB")
    mcu = env.BoardConfig().get("build.mcu", "esp32s3")

    # Convert numeric frequency to esptool format
    freq_map = {"80000000": "80m", "40000000": "40m", "26000000": "26m", "20000000": "20m"}
    flash_freq_str = freq_map.get(flash_freq, "80m")

    cmd = [
        env.subst("$PYTHONEXE"), "-m", "esptool",
        "--chip", mcu,
        "merge_bin",
        "-o", output,
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq_str,
        "--flash_size", flash_size,
        "0x0",     bootloader,
        "0x8000",  partitions,
        "0x10000", firmware,
    ]

    print(f"\n[merge] Creating merged firmware for {env_name}...")
    print(f"[merge] {' '.join(cmd[-6:])}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        size_kb = os.path.getsize(output) / 1024
        print(f"[merge] OK: {output} ({size_kb:.0f} KB)")
    else:
        print(f"[merge] FAILED: {result.stderr}")

env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)