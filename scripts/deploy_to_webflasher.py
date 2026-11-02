#!/usr/bin/env python3
"""Build rdz_ttgo_sonde firmware and deploy it to the hoan.uk Pi (webflasher + in-app OTA).

Everything is staged in a local temp dir that mirrors /home/pi/hoan.uk/firmware/ and
then pushed with rsync over ssh. Use --dry-run to see the actions without touching the server.
"""
import os
import re
import sys
import json
import shutil
import subprocess
import argparse
import tempfile
from datetime import datetime

# Configured paths
RDZ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(RDZ_DIR, "RX_FSK/data")
VERSION_H = os.path.join(RDZ_DIR, "RX_FSK/version.h")

REMOTE_HOST = "root@192.168.3.24"
REMOTE_FW_DIR = "/home/pi/hoan.uk/firmware"
REMOTE_OWNER = "pi:pi"

SSH_OPTS = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10"]
BUILD_TIMEOUT = 1800
NET_TIMEOUT = 300

ENV_CONFIGS = {
    "heltec-lora32-v3": {
        "board_name": "Heltec-lora-32-V3",
        "folder_name": "heltec-wifi-lora-v3",
        "mcu": "esp32s3",
        "flash_size": "8MB",
        "flash_mode": "qio",
        "fs_offset": "0x320000",
        "fonts_offset": "0x310000",
    },
    "ttgo-lora32": {
        "board_name": "Sonde-TTGO-Lora32-v21",
        "folder_name": "ttgo-lora32",
        "mcu": "esp32",
        "flash_size": "4MB",
        "flash_mode": "dio",
        "fs_offset": "0x320000",
        "fonts_offset": "0x310000",
    }
}

# User state on the device: must never be overwritten by an OTA filesystem update.
USER_STATE_FILES = {"config.txt", "networks.txt", "qrg.txt", "user.txt", "lastpos.txt", "localupd.txt"}

DRY_RUN = False


def run_cmd(cmd, cwd=None, timeout=NET_TIMEOUT, capture=False):
    print(f"Running command: {' '.join(cmd)}")
    try:
        res = subprocess.run(cmd, cwd=cwd, timeout=timeout,
                             stdout=subprocess.PIPE if capture else None, text=capture)
    except subprocess.TimeoutExpired:
        print(f"Error: Command timed out after {timeout}s")
        sys.exit(1)
    if res.returncode != 0:
        print(f"Error: Command failed with code {res.returncode}")
        sys.exit(res.returncode)
    return res.stdout if capture else None


def ssh(remote_cmd, capture=False, timeout=60):
    return run_cmd(["ssh"] + SSH_OPTS + [REMOTE_HOST, remote_cmd], timeout=timeout, capture=capture)


def read_version_id():
    """Reads version_id (e.g. 'v1.3.6-hoanuk') from RX_FSK/version.h."""
    with open(VERSION_H) as f:
        m = re.search(r'version_id\s*=\s*"([^"]+)"', f.read())
    if not m:
        print(f"Error: version_id not found in {VERSION_H}")
        sys.exit(1)
    return m.group(1)


def folder_version(version_id):
    """'v1.3.6-hoanuk' -> '1.3.6'."""
    m = re.match(r'v?(\d+(?:\.\d+)*)', version_id)
    if not m:
        print(f"Error: cannot derive folder version from '{version_id}'")
        sys.exit(1)
    return m.group(1)


def generate_update_fs_bin(data_dir, output_file):
    """Packages HTML, CSS, JS and screen/gps definition files from data_dir into update.fs.bin.
    User state files (config, networks, ...) are excluded so an OTA does not wipe them."""
    print(f"Generating update.fs.bin from {data_dir} -> {output_file}")
    files = sorted(
        f for f in os.listdir(data_dir)
        if f.endswith(('.js', '.html', '.css', '.txt'))
        and f not in USER_STATE_FILES
        and os.path.isfile(os.path.join(data_dir, f))
    )
    with open(output_file, "wb") as out_f:
        for f in files:
            with open(os.path.join(data_dir, f), "rb") as in_f:
                data = in_f.read()
            out_f.write(f"{f} {len(data)}\r\n".encode("utf-8"))
            out_f.write(data)
    print(f"  packed {len(files)} files: {', '.join(files)}")


def build_env(env, skip_build):
    cfg = ENV_CONFIGS[env]
    build_dir = os.path.join(RDZ_DIR, f".pio/build/{env}")

    if not skip_build:
        print(f"[{env}] Building default binary targets...")
        run_cmd(["pio", "run", "-e", env], cwd=RDZ_DIR, timeout=BUILD_TIMEOUT)
        print(f"[{env}] Building filesystem binary targets...")
        run_cmd(["pio", "run", "-e", env, "-t", "buildfs"], cwd=RDZ_DIR, timeout=BUILD_TIMEOUT)
    else:
        print(f"[{env}] --skip-build: reusing {build_dir}")

    # Merged full flash image (always regenerated from the build outputs)
    print(f"[{env}] Building merged full flash image...")
    mcu = cfg["mcu"]
    bootloader_offset = "0x0" if mcu == "esp32s3" else "0x1000"
    merge_cmd = [
        "pio", "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py",
        "--chip", mcu,
        "merge_bin",
        "-o", os.path.join(build_dir, "firmware-image.bin"),
        "--flash_mode", cfg["flash_mode"],
        "--flash_size", cfg["flash_size"],
        "--pad-to-size", cfg["flash_size"],
        bootloader_offset, os.path.join(build_dir, "bootloader.bin"),
        "0x8000", os.path.join(build_dir, "partitions.bin"),
        "0x10000", os.path.join(build_dir, "firmware.bin"),
        cfg["fonts_offset"], os.path.join(build_dir, "fonts.bin"),
        cfg["fs_offset"], os.path.join(build_dir, "littlefs.bin"),
    ]
    run_cmd(merge_cmd, cwd=RDZ_DIR, timeout=BUILD_TIMEOUT)

    outs = {k: os.path.join(build_dir, v) for k, v in
            [("ota", "firmware.bin"), ("full", "firmware-image.bin"),
             ("fs", "littlefs.bin"), ("fonts", "fonts.bin")]}
    for f in outs.values():
        if not os.path.exists(f):
            print(f"Error: Required build output not found: {f}")
            sys.exit(1)
    return outs


def stage_env(env, version, outs, stage_dir):
    """Copies binaries into stage_dir/<folder>/<version>/ and returns the files list for version.json."""
    cfg = ENV_CONFIGS[env]
    fn = cfg["folder_name"]
    dest = os.path.join(stage_dir, fn, version)
    os.makedirs(dest, exist_ok=True)
    names = {
        "ota": f"ota-{fn}-{version}.bin",
        "full": f"full-image-{fn}-{version}.bin",
        "fs": f"littlefs-{fn}-{version}.bin",
        "fonts": f"fonts-{fn}-{version}.bin",
    }
    for k, name in names.items():
        shutil.copy2(outs[k], os.path.join(dest, name))

    base = f"/firmware/{fn}/{version}"
    return [
        {"name": "Firmware (OTA)", "type": "ota", "url": f"{base}/{names['ota']}", "offset": "0x10000"},
        {"name": "Full Image (USB)", "type": "full", "url": f"{base}/{names['full']}", "offset": "0x0"},
        {"name": "Fonts Partition", "type": "fonts", "url": f"{base}/{names['fonts']}", "offset": cfg["fonts_offset"]},
        {"name": "FileSystem (OTA)", "type": "fs", "url": f"{base}/{names['fs']}", "offset": cfg["fs_offset"]},
    ]


def update_version_json(data, env, version, files_list):
    cfg = ENV_CONFIGS[env]
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

    if board_entry:
        board_entry["version"] = version
        board_entry["files"] = files_list
        print(f"[{env}] Updated existing entry for '{cfg['board_name']}' in version.json")
    else:
        data.setdefault("boards", []).append({
            "name": cfg["board_name"],
            "version": version,
            "files": files_list,
        })
        print(f"[{env}] Added new entry for '{cfg['board_name']}' in version.json")


def stage_ota_packages(version_id, channels, ota_bin, stage_dir):
    """Generates stage_dir/rdz/<channel>/{update.ino.bin,update.fs.bin,update-info.html}."""
    print("\n--- Generating RDZ in-app OTA Update Endpoints ---")
    tmp_fs_bin = os.path.join(stage_dir, "update.fs.bin.tmp")
    generate_update_fs_bin(DATA_DIR, tmp_fs_bin)
    # update-info.html must be exactly the firmware version_id (e.g. v1.3.6-hoanuk)
    update_info_html = f"<html><body><p>{version_id}</p></body></html>\n"

    for ch in channels:
        d = os.path.join(stage_dir, "rdz", ch)
        os.makedirs(d, exist_ok=True)
        shutil.copy2(tmp_fs_bin, os.path.join(d, "update.fs.bin"))
        shutil.copy2(ota_bin, os.path.join(d, "update.ino.bin"))
        with open(os.path.join(d, "update-info.html"), "w") as f:
            f.write(update_info_html)
    os.remove(tmp_fs_bin)
    print(f"Staged OTA files for channels {channels} (using {ota_bin})")


def main():
    global DRY_RUN
    parser = argparse.ArgumentParser(description="Build and deploy rdz_ttgo_sonde firmware to the hoan.uk Pi (webflasher and OTA endpoints)")
    parser.add_argument("--env", choices=list(ENV_CONFIGS.keys()) + ["all"], default="heltec-lora32-v3",
                        help="PlatformIO environment to build and deploy (default: heltec-lora32-v3)")
    parser.add_argument("--version-str", default=None,
                        help="Folder version, e.g. 1.3.6 (default: derived from version_id in RX_FSK/version.h)")
    parser.add_argument("--channels", default="main,dev2", help="Comma-separated OTA channels under rdz/ (default: main,dev2)")
    parser.add_argument("--skip-build", action="store_true", help="Reuse existing .pio/build outputs, do not run pio")
    parser.add_argument("--dry-run", action="store_true", help="Print server actions without changing anything on the server")
    args = parser.parse_args()
    DRY_RUN = args.dry_run

    version_id = read_version_id()
    version = args.version_str or folder_version(version_id)
    channels = [c.strip() for c in args.channels.split(",") if c.strip()]
    envs = list(ENV_CONFIGS.keys()) if args.env == "all" else [args.env]
    print(f"version_id={version_id} folder version={version} envs={envs} channels={channels} dry_run={DRY_RUN}")

    stage_dir = tempfile.mkdtemp(prefix="webflasher-stage-")
    try:
        # Step 1: build + stage
        built = {}
        for e in envs:
            print(f"\n=== {e} (v{version}) ===")
            built[e] = build_env(e, args.skip_build)

        # Step 2: fetch version.json
        remote_vj = f"{REMOTE_FW_DIR}/version.json"
        local_vj = os.path.join(stage_dir, "version.json")
        if DRY_RUN:
            print(f"[dry-run] would fetch {REMOTE_HOST}:{remote_vj}")
            data = {"boards": []}
        else:
            run_cmd(["scp"] + SSH_OPTS + [f"{REMOTE_HOST}:{remote_vj}", local_vj], timeout=NET_TIMEOUT)
            with open(local_vj) as f:
                data = json.load(f)

        for e in envs:
            files_list = stage_env(e, version, built[e], stage_dir)
            update_version_json(data, e, version, files_list)
        with open(local_vj, "w") as f:
            json.dump(data, f, indent=2)

        # Step 3: OTA channel files (prefer the first selected env's firmware)
        stage_ota_packages(version_id, channels, built[envs[0]]["ota"], stage_dir)

        # Step 4: upload
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        if DRY_RUN:
            print("\n[dry-run] staged tree:")
            for root, _, fs in os.walk(stage_dir):
                for fn in sorted(fs):
                    p = os.path.join(root, fn)
                    print(f"  {os.path.relpath(p, stage_dir)} ({os.path.getsize(p)} B)")
            for ch in channels:
                print(f"[dry-run] ssh: cp -a {REMOTE_FW_DIR}/rdz/{ch} {REMOTE_FW_DIR}/rdz/{ch}.bak-{stamp}")
            print(f"[dry-run] ssh: cp -a {remote_vj} {remote_vj}.bak-{stamp}")
            print(f"[dry-run] rsync -rt {stage_dir}/ {REMOTE_HOST}:{REMOTE_FW_DIR}/")
            print(f"[dry-run] ssh: chown -R {REMOTE_OWNER} {REMOTE_FW_DIR}")
            print("\n--- DRY RUN FINISHED, nothing changed on the server ---")
            return

        backups = " && ".join(
            f"if [ -d {REMOTE_FW_DIR}/rdz/{ch} ]; then cp -a {REMOTE_FW_DIR}/rdz/{ch} {REMOTE_FW_DIR}/rdz/{ch}.bak-{stamp}; fi"
            for ch in channels)
        ssh(f"{backups} && cp -a {remote_vj} {remote_vj}.bak-{stamp}")
        run_cmd(["rsync", "-rt", "-e", "ssh " + " ".join(SSH_OPTS), stage_dir + "/", f"{REMOTE_HOST}:{REMOTE_FW_DIR}/"],
                timeout=NET_TIMEOUT)
        subdirs = [ENV_CONFIGS[e]["folder_name"] for e in envs] + ["rdz", "version.json"]
        ssh("chown -R {} {}".format(REMOTE_OWNER, " ".join(f"{REMOTE_FW_DIR}/{s}" for s in subdirs)))
        print("\n--- ALL DEPLOYMENTS FINISHED SUCCESSFULLY! ---")
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
