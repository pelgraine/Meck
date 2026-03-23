"""
PlatformIO post-build script: merge bootloader + partitions + firmware + SPIFFS
into a single flashable binary.

Includes a pre-formatted empty SPIFFS image so first-boot doesn't need to
format the partition (which takes 1-2 minutes on 16MB flash).

Output: .pio/build/<env>/firmware_merged.bin
Flash:  esptool.py --chip esp32s3 write_flash 0x0 firmware_merged.bin

Place this file in the project root alongside platformio.ini.
Add to each environment (or the base section):
  extra_scripts = post:merge_firmware.py
"""

Import("env")

def find_spiffs_partition(partitions_bin):
    """Parse compiled partitions.bin to find SPIFFS partition offset and size.
    
    ESP32 partition entry format (32 bytes each):
      0xAA50 magic, type, subtype, offset(u32le), size(u32le), label(16), flags(u32le)
    SPIFFS: type=0x01(data), subtype=0x82(spiffs)
    """
    import struct

    with open(partitions_bin, "rb") as f:
        data = f.read()

    for i in range(0, len(data) - 32, 32):
        magic = struct.unpack_from("<H", data, i)[0]
        if magic != 0xAA50:
            continue
        ptype = data[i + 2]
        subtype = data[i + 3]
        offset = struct.unpack_from("<I", data, i + 4)[0]
        size = struct.unpack_from("<I", data, i + 8)[0]
        label = data[i + 12:i + 28].split(b'\x00')[0].decode("ascii", errors="ignore")
        if ptype == 0x01 and subtype == 0x82:  # data/spiffs
            return offset, size, label
    return None, None, None


def build_spiffs_image(env, size):
    """Generate an empty formatted SPIFFS image using mkspiffs."""
    import subprocess, os, tempfile, glob

    build_dir = env.subst("$BUILD_DIR")
    spiffs_bin = os.path.join(build_dir, "spiffs_empty.bin")

    # If already generated for this build, reuse it
    if os.path.isfile(spiffs_bin) and os.path.getsize(spiffs_bin) == size:
        return spiffs_bin

    # Find mkspiffs in PlatformIO packages
    pio_home = os.path.expanduser("~/.platformio")
    mkspiffs_paths = glob.glob(os.path.join(pio_home, "packages", "tool-mkspiffs*", "mkspiffs*"))
    if not mkspiffs_paths:
        # Also check platform-specific tool paths
        mkspiffs_paths = glob.glob(os.path.join(pio_home, "packages", "tool-mklittlefs*", "mkspiffs*"))
    
    mkspiffs = None
    for p in mkspiffs_paths:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            mkspiffs = p
            break

    if not mkspiffs:
        print("[merge] WARNING: mkspiffs not found, skipping SPIFFS image")
        return None

    # Create empty data directory for mkspiffs
    data_dir = os.path.join(build_dir, "_empty_spiffs_data")
    os.makedirs(data_dir, exist_ok=True)

    # SPIFFS block/page sizes — ESP32 Arduino defaults
    block_size = 4096
    page_size = 256

    cmd = [
        mkspiffs,
        "-c", data_dir,
        "-b", str(block_size),
        "-p", str(page_size),
        "-s", str(size),
        spiffs_bin,
    ]

    print(f"[merge] Generating empty SPIFFS image ({size // 1024} KB)...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0 and os.path.isfile(spiffs_bin):
        print(f"[merge] SPIFFS image OK: {spiffs_bin}")
        return spiffs_bin
    else:
        print(f"[merge] mkspiffs failed: {result.stderr}")
        return None


def merge_bin(source, target, env):
    import subprocess, os

    build_dir = env.subst("$BUILD_DIR")
    env_name = env.subst("$PIOENV")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")
    output     = os.path.join(build_dir, "firmware-merged.bin")

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

    # Try to include a pre-formatted SPIFFS image (eliminates 1-2 min first-boot format)
    spiffs_offset, spiffs_size, spiffs_label = find_spiffs_partition(partitions)
    if spiffs_offset and spiffs_size:
        spiffs_bin = build_spiffs_image(env, spiffs_size)
        if spiffs_bin:
            cmd.extend([f"0x{spiffs_offset:x}", spiffs_bin])
            print(f"[merge] Including SPIFFS image at 0x{spiffs_offset:x} ({spiffs_size // 1024} KB)")
    else:
        print("[merge] No SPIFFS partition found in partition table, skipping SPIFFS image")

    print(f"\n[merge] Creating merged firmware for {env_name}...")
    print(f"[merge] {' '.join(cmd[-8:])}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        size_kb = os.path.getsize(output) / 1024
        print(f"[merge] OK: {output} ({size_kb:.0f} KB)")
    else:
        print(f"[merge] FAILED: {result.stderr}")

env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)