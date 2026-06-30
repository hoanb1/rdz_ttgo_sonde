#!/usr/bin/env python3
import os
import sys
import json
import shutil
import subprocess
import argparse

# Configured paths
RDZ_DIR = "/home/hoan/DATA/rdz_ttgo_sonde"
HOAN_DIR = "/home/hoan/DATA/hoan.uk"
VERSION_JSON_PATH = os.path.join(HOAN_DIR, "firmware/version.json")

ENV_CONFIGS = {
    "heltec-lora32-v3": {
        "board_name": "Heltec-lora-32-V3",
        "folder_name": "heltec-wifi-lora-v3",
        "fs_offset": "0x320000",
        "fonts_offset": "0x310000",
    },
    "ttgo-lora32": {
        "board_name": "Sonde-TTGO-Lora32-v21",
        "folder_name": "ttgo-lora32",
        "fs_offset": "0x320000",
        "fonts_offset": "0x310000",
    }
}

def run_cmd(cmd, cwd=None):
    print(f"Running command: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        print(f"Error: Command failed with code {res.returncode}")
        sys.exit(res.returncode)

def main():
    parser = argparse.ArgumentParser(description="Build and copy rdz_ttgo_sonde firmware to hoan.uk webflasher")
    parser.add_argument("--env", choices=list(ENV_CONFIGS.keys()), required=True, help="PlatformIO environment to build and deploy")
    parser.add_argument("--version-str", default="1.0.0", help="Firmware version (default: 1.0.0)")
    args = parser.parse_args()

    env = args.env
    cfg = ENV_CONFIGS[env]
    version = args.version_str
    build_dir = os.path.join(RDZ_DIR, f".pio/build/{env}")

    print(f"--- Starting deployment for {env} (v{version}) ---")

    # Step 1: Compile firmware
    print("Building default binary targets...")
    run_cmd(["pio", "run", "-e", env], cwd=RDZ_DIR)

    # Step 2: Build filesystem (LittleFS)
    print("Building filesystem binary targets...")
    run_cmd(["pio", "run", "-e", env, "-t", "buildfs"], cwd=RDZ_DIR)

    # Step 3: Build merged firmware image using the correct esptool path
    print("Building merged full flash image...")
    esptool_path = os.path.expanduser("~/.platformio/penv/bin/esptool")
    if not os.path.exists(esptool_path):
        esptool_path = "esptool"  # fallback to PATH
    
    mcu = "esp32s3" if env == "heltec-lora32-v3" else "esp32"
    bootloader_offset = "0x0" if mcu == "esp32s3" else "0x1000"
    
    # We need to construct the esptool merge_bin command
    merge_cmd = [
        esptool_path,
        "--chip", mcu,
        "merge_bin",
        "-o", os.path.join(build_dir, "firmware-image.bin"),
        "--flash_mode", "dio",
        "--flash_size", "4MB",
        bootloader_offset, os.path.join(build_dir, "bootloader.bin"),
        "0x8000", os.path.join(build_dir, "partitions.bin"),
        "0x10000", os.path.join(build_dir, "firmware.bin"),
        "0x310000", os.path.join(build_dir, "fonts.bin"),
        "0x320000", os.path.join(build_dir, "littlefs.bin"),
        "--target-offset", bootloader_offset
    ]
    run_cmd(merge_cmd, cwd=RDZ_DIR)

    # Step 4: Locate generated binaries
    src_ota = os.path.join(build_dir, "firmware.bin")
    src_full = os.path.join(build_dir, "firmware-image.bin")
    src_fs = os.path.join(build_dir, "littlefs.bin")
    src_fonts = os.path.join(build_dir, "fonts.bin")

    # Verify they exist
    for f in [src_ota, src_full, src_fs, src_fonts]:
        if not os.path.exists(f):
            print(f"Error: Required build output not found: {f}")
            sys.exit(1)

    # Step 5: Copy files to hoan.uk
    dest_dir = os.path.join(HOAN_DIR, f"firmware/{cfg['folder_name']}/{version}")
    os.makedirs(dest_dir, exist_ok=True)

    dest_ota_name = f"ota-{cfg['folder_name']}-{version}.bin"
    dest_full_name = f"full-image-{cfg['folder_name']}-{version}.bin"
    dest_fs_name = f"littlefs-{cfg['folder_name']}-{version}.bin"
    dest_fonts_name = f"fonts-{cfg['folder_name']}-{version}.bin"

    shutil.copy2(src_ota, os.path.join(dest_dir, dest_ota_name))
    shutil.copy2(src_full, os.path.join(dest_dir, dest_full_name))
    shutil.copy2(src_fs, os.path.join(dest_dir, dest_fs_name))
    shutil.copy2(src_fonts, os.path.join(dest_dir, dest_fonts_name))

    print(f"Successfully copied binaries to: {dest_dir}")

    # Step 6: Update version.json
    if not os.path.exists(VERSION_JSON_PATH):
        print(f"Error: version.json not found at {VERSION_JSON_PATH}")
        sys.exit(1)

    with open(VERSION_JSON_PATH, "r") as f:
        data = json.load(f)

    # Standardize all existing board URLs to relative paths
    for board in data.get("boards", []):
        for f_info in board.get("files", []):
            url = f_info.get("url", "")
            if url.startswith("https://hoan.uk/"):
                f_info["url"] = url.replace("https://hoan.uk", "")

    # Find if this board already exists
    board_entry = None
    for b in data.get("boards", []):
        if b.get("name", "").lower() == cfg["board_name"].lower():
            board_entry = b
            break

    # Construct the file entry configs
    files_list = [
        {
            "name": "Firmware (OTA)",
            "type": "ota",
            "url": f"/firmware/{cfg['folder_name']}/{version}/{dest_ota_name}",
            "offset": "0x10000"
        },
        {
            "name": "Full Image (USB)",
            "type": "full",
            "url": f"/firmware/{cfg['folder_name']}/{version}/{dest_full_name}",
            "offset": "0x0"
        },
        {
            "name": "Fonts Partition",
            "type": "fonts",
            "url": f"/firmware/{cfg['folder_name']}/{version}/{dest_fonts_name}",
            "offset": cfg["fonts_offset"]
        },
        {
            "name": "FileSystem (OTA)",
            "type": "fs",
            "url": f"/firmware/{cfg['folder_name']}/{version}/{dest_fs_name}",
            "offset": cfg["fs_offset"]
        }
    ]

    if board_entry:
        # Update existing board entry
        board_entry["version"] = version
        board_entry["files"] = files_list
        print(f"Updated existing entry for '{cfg['board_name']}' in version.json")
    else:
        # Append new board entry
        new_board = {
            "name": cfg["board_name"],
            "version": version,
            "files": files_list
        }
        data["boards"].append(new_board)
        print(f"Added new entry for '{cfg['board_name']}' in version.json")

    with open(VERSION_JSON_PATH, "w") as f:
        json.dump(data, f, indent=2)

    print("Successfully updated version.json!")
    print("--- Deployment Complete! ---")

if __name__ == "__main__":
    main()
