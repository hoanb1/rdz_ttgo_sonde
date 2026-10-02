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

def generate_update_fs_bin(data_dir, output_file):
    """Packages HTML, CSS, and JS files from data_dir into update.fs.bin."""
    print(f"Generating update.fs.bin from {data_dir} -> {output_file}")
    files = [f for f in os.listdir(data_dir) if f.endswith(('.js', '.html', '.css', '.txt'))]
    with open(output_file, "wb") as out_f:
        for f in files:
            file_path = os.path.join(data_dir, f)
            with open(file_path, "rb") as in_f:
                data = in_f.read()
            header = f"{f} {len(data)}\r\n".encode("utf-8")
            out_f.write(header)
            out_f.write(data)

def deploy_env(env, version):
    cfg = ENV_CONFIGS[env]
    build_dir = os.path.join(RDZ_DIR, f".pio/build/{env}")

    print(f"\n=======================================================")
    print(f"--- Starting deployment for {env} (v{version}) ---")
    print(f"=======================================================\n")

    # Step 1: Compile firmware
    print(f"[{env}] Building default binary targets...")
    run_cmd(["pio", "run", "-e", env], cwd=RDZ_DIR)

    # Step 2: Build filesystem (LittleFS)
    print(f"[{env}] Building filesystem binary targets...")
    run_cmd(["pio", "run", "-e", env, "-t", "buildfs"], cwd=RDZ_DIR)

    # Step 3: Build merged firmware image using esptool
    print(f"[{env}] Building merged full flash image...")
    esptool_path = os.path.expanduser("~/.platformio/penv/bin/esptool")
    if not os.path.exists(esptool_path):
        esptool_path = shutil.which("esptool.py") or shutil.which("esptool") or "esptool.py"
    
    mcu = "esp32s3" if env == "heltec-lora32-v3" else "esp32"
    bootloader_offset = "0x0" if mcu == "esp32s3" else "0x1000"
    
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

    for f in [src_ota, src_full, src_fs, src_fonts]:
        if not os.path.exists(f):
            print(f"Error: Required build output not found: {f}")
            sys.exit(1)

    # Step 5: Copy files to hoan.uk locations
    dest_dir = os.path.join(HOAN_DIR, f"firmware/{cfg['folder_name']}/{version}")
    public_dest_dir = os.path.join(HOAN_DIR, f"client/public/firmware/{cfg['folder_name']}/{version}")
    dist_dest_dir = os.path.join(HOAN_DIR, f"client/dist/firmware/{cfg['folder_name']}/{version}")

    dest_ota_name = f"ota-{cfg['folder_name']}-{version}.bin"
    dest_full_name = f"full-image-{cfg['folder_name']}-{version}.bin"
    dest_fs_name = f"littlefs-{cfg['folder_name']}-{version}.bin"
    dest_fonts_name = f"fonts-{cfg['folder_name']}-{version}.bin"

    for target_dir in [dest_dir, public_dest_dir, dist_dest_dir]:
        os.makedirs(target_dir, exist_ok=True)
        shutil.copy2(src_ota, os.path.join(target_dir, dest_ota_name))
        shutil.copy2(src_full, os.path.join(target_dir, dest_full_name))
        shutil.copy2(src_fs, os.path.join(target_dir, dest_fs_name))
        shutil.copy2(src_fonts, os.path.join(target_dir, dest_fonts_name))

    print(f"[{env}] Copied firmware binaries to server & client directories.")

    # Step 6: Update version.json
    with open(VERSION_JSON_PATH, "r") as f:
        data = json.load(f)

    # Standardize URLs
    for board in data.get("boards", []):
        for f_info in board.get("files", []):
            url = f_info.get("url", "")
            if url.startswith("https://hoan.uk/"):
                f_info["url"] = url.replace("https://hoan.uk", "")

    board_entry = None
    for b in data.get("boards", []):
        if b.get("name", "").lower() == cfg["board_name"].lower():
            board_entry = b
            break

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
        board_entry["version"] = version
        board_entry["files"] = files_list
        print(f"[{env}] Updated existing entry for '{cfg['board_name']}' in version.json")
    else:
        new_board = {
            "name": cfg["board_name"],
            "version": version,
            "files": files_list
        }
        data["boards"].append(new_board)
        print(f"[{env}] Added new entry for '{cfg['board_name']}' in version.json")

    with open(VERSION_JSON_PATH, "w") as f:
        json.dump(data, f, indent=2)

    # Also sync version.json to public and dist
    for p in [os.path.join(HOAN_DIR, "client/public/firmware/version.json"), os.path.join(HOAN_DIR, "client/dist/firmware/version.json")]:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        shutil.copy2(VERSION_JSON_PATH, p)

def deploy_ota_packages(version):
    """Generates /firmware/rdz/main/ and /firmware/rdz/dev2/ OTA update endpoints."""
    print("\n=======================================================")
    print("--- Generating RDZ in-app OTA Update Endpoints ---")
    print("=======================================================\n")

    data_dir = os.path.join(RDZ_DIR, "RX_FSK/data")
    tmp_fs_bin = os.path.join(RDZ_DIR, ".pio/build/update.fs.bin")
    os.makedirs(os.path.dirname(tmp_fs_bin), exist_ok=True)
    generate_update_fs_bin(data_dir, tmp_fs_bin)

    update_info_html = f"<html><body><p>v{version}-hoanuk</p></body></html>\n"

    ttgo_bin = os.path.join(RDZ_DIR, ".pio/build/ttgo-lora32/firmware.bin")
    if not os.path.exists(ttgo_bin):
        ttgo_bin = os.path.join(RDZ_DIR, ".pio/build/heltec-lora32-v3/firmware.bin")

    channels = ["main", "dev2"]
    for ch in channels:
        for root_prefix in [
            os.path.join(HOAN_DIR, f"firmware/rdz/{ch}"),
            os.path.join(HOAN_DIR, f"client/public/firmware/rdz/{ch}"),
            os.path.join(HOAN_DIR, f"client/dist/firmware/rdz/{ch}")
        ]:
            os.makedirs(root_prefix, exist_ok=True)
            # 1. update.fs.bin
            shutil.copy2(tmp_fs_bin, os.path.join(root_prefix, "update.fs.bin"))
            # 2. update.ino.bin
            if os.path.exists(ttgo_bin):
                shutil.copy2(ttgo_bin, os.path.join(root_prefix, "update.ino.bin"))
            # 3. update-info.html
            with open(os.path.join(root_prefix, "update-info.html"), "w") as f:
                f.write(update_info_html)

    print(f"Successfully generated OTA files in /firmware/rdz/main/ and /firmware/rdz/dev2/")

def main():
    parser = argparse.ArgumentParser(description="Build and copy rdz_ttgo_sonde firmware to hoan.uk webflasher and OTA endpoints")
    parser.add_argument("--env", choices=list(ENV_CONFIGS.keys()) + ["all"], default="all", help="PlatformIO environment to build and deploy (default: all)")
    parser.add_argument("--version-str", default="1.3.1", help="Firmware version (default: 1.3.1)")
    args = parser.parse_args()

    version = args.version_str

    if args.env == "all":
        for e in ENV_CONFIGS.keys():
            deploy_env(e, version)
    else:
        deploy_env(args.env, version)

    deploy_ota_packages(version)
    print("\n--- ALL DEPLOYMENTS FINISHED SUCCESSFULLY! ---")

if __name__ == "__main__":
    main()

