#!/usr/bin/env python3
"""Bound Wi-Fi experiments with an independent, reboot-safe systemd recovery.

Requires root. No connection profile or password is copied or modified.
The installed timer must remain enabled until no armed experiment remains.
"""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path("/var/lib/trailmate-espnow-probe")
STATE = ROOT / "armed.json"
BOOT = Path("/proc/sys/kernel/random/boot_id")
TIMER = "trailmate-espnow-recover.timer"


def radio():
    path = Path("/usr/local/libexec/trailmate-espnow/linux_espnow_probe.py")
    spec = importlib.util.spec_from_file_location("trailmate_radio", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.Firmware()


def prepare_radio():
    state = json.loads(STATE.read_text())
    firmware = radio()
    try:
        state["radio_power"] = firmware.power_policy()
        if state["radio_power"]["mpc"] not in (0, 1) or state["radio_power"]["pm"] not in (0, 1, 2):
            raise RuntimeError("Unsupported original radio power policy; no power change")
        atomic_json(STATE, state)
        firmware.set_power_policy({"mpc": 0, "pm": 0})
        print(json.dumps({"radio_power_saved": state["radio_power"], "active": {"mpc": 0, "pm": 0}}), flush=True)
    finally:
        firmware.close()


def command(*argv, timeout=12, check=True):
    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    print(json.dumps({"command": list(argv), "code": result.returncode,
                      "output": result.stdout.strip()}), flush=True)
    if check and result.returncode:
        raise RuntimeError("Command failed: " + argv[0])
    return result.stdout.strip()


def atomic_json(path, value):
    temp = path.with_suffix(".new")
    with temp.open("w") as stream:
        os.chmod(temp, 0o600)
        json.dump(value, stream)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def arm(seconds):
    if STATE.exists():
        raise RuntimeError("A previous experiment is still armed")
    command("systemctl", "is-enabled", TIMER)
    command("systemctl", "is-active", TIMER)
    command("systemctl", "is-active", "NetworkManager")
    uuid = command("nmcli", "-g", "GENERAL.CON-UUID", "device", "show", "wlan0")
    if not uuid or uuid == "--":
        raise RuntimeError("wlan0 must have a recoverable active connection")
    if not command("ip", "-4", "route", "show", "default", "dev", "wlan0"):
        raise RuntimeError("A recoverable IPv4 default route is required")
    state = {"uuid": uuid, "interface": "wlan0", "boot_id": BOOT.read_text().strip(),
             "deadline_boottime": time.clock_gettime(time.CLOCK_BOOTTIME) + seconds,
             "armed_unix": time.time(), "timeout_seconds": seconds}
    atomic_json(STATE, state)
    print(json.dumps({"armed": state}), flush=True)


def recover(force=False):
    if not STATE.exists():
        return
    state = json.loads(STATE.read_text())
    if (not force and state["boot_id"] == BOOT.read_text().strip()
            and time.clock_gettime(time.CLOCK_BOOTTIME) < state["deadline_boottime"]):
        return
    # Never overlap the radio experiment and recovery. This unit has a bounded
    # timeout and KillMode=control-group, so no worker can keep changing wlan0.
    command("systemctl", "stop", "trailmate-espnow-test.service", timeout=12, check=False)
    command("systemctl", "stop", "trailmate-espnow-helper.service", timeout=12, check=False)
    command("systemctl", "start", "NetworkManager", timeout=20)
    command("rfkill", "unblock", "wifi")
    info = command("iw", "dev", "wlan0", "info")
    if "type managed" not in info:
        command("ip", "link", "set", "wlan0", "down")
        command("iw", "dev", "wlan0", "set", "type", "managed")
    command("nmcli", "radio", "wifi", "on")
    command("nmcli", "device", "set", "wlan0", "managed", "yes")
    command("ip", "link", "set", "wlan0", "up")
    if "radio_power" in state:
        firmware = radio()
        try:
            firmware.set_power_policy(state["radio_power"])
        finally:
            firmware.close()
    command("nmcli", "--wait", "25", "connection", "up", "uuid", state["uuid"],
            "ifname", "wlan0", timeout=30)
    current = command("nmcli", "-g", "GENERAL.CON-UUID", "device", "show", "wlan0")
    route = command("ip", "-4", "route", "show", "default", "dev", "wlan0")
    if current != state["uuid"] or not route:
        raise RuntimeError("Recovery not confirmed; retain state and retry")
    atomic_json(ROOT / "last-recovery.json", {
        "recovered_unix": time.time(), "armed": state, "route": route,
        "recovery_boot_id": BOOT.read_text().strip()})
    STATE.unlink()
    print("RECOVERY_CONFIRMED", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("arm", "prepare-radio", "recover", "restore", "expire", "status"))
    parser.add_argument("--seconds", type=int, default=45)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("root is required")
    if not 15 <= args.seconds <= 180:
        parser.error("deadline must be between 15 and 180 seconds")
    ROOT.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (ROOT / "lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.action == "arm":
            arm(args.seconds)
        elif args.action == "prepare-radio":
            prepare_radio()
        elif args.action == "expire":
            if STATE.exists():
                state = json.loads(STATE.read_text())
                state["deadline_boottime"] = 0
                atomic_json(STATE, state)
        elif args.action in ("recover", "restore"):
            recover(force=args.action == "restore")
        else:
            print(STATE.read_text() if STATE.exists() else "UNARMED")


if __name__ == "__main__":
    main()
