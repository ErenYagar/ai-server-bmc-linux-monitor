#!/usr/bin/env python3
"""Local simulator operator CLI; Python standard library only."""
import argparse
from collections import deque
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def state():
    data = json.loads((ROOT / "runtime/state.json").read_text())
    if not isinstance(data, dict):
        raise ValueError("state must be a JSON object")
    age = (datetime.now(timezone.utc) -
           datetime.fromisoformat(data["timestamp"])).total_seconds()
    if age < -1 or age > 5:
        raise ValueError("state is stale; start the monitor daemon")
    return data


def write_sensor(name, value):
    target = ROOT / "mock_hwmon" / name
    temporary = target.with_suffix(".tmp")
    temporary.write_text(f"{value}\n")
    temporary.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("status", "sensors", "logs", "recover"):
        commands.add_parser(name)
    inject = commands.add_parser("inject")
    inject.add_argument("fault", choices=(
        "gpu-overheat", "fan-stall", "psu-undervolt", "sensor-missing"))
    args = parser.parse_args()
    try:
        if args.command == "recover":
            subprocess.run(["bash", str(ROOT / "scripts/reset_mock.sh")], check=True)
            print("Recovered mock sensors: CPU=55 C GPU=65 C FAN=3200 RPM PSU=12.1 V")
        elif args.command == "inject":
            values = {"gpu-overheat": ("gpu_temp", "98.0"),
                      "fan-stall": ("fan_rpm", "0"),
                      "psu-undervolt": ("psu_voltage", "10.8")}
            if args.fault == "sensor-missing":
                sensor = ROOT / "mock_hwmon/gpu_temp"
                missing = sensor.with_suffix(".missing")
                if sensor.exists() and missing.exists():
                    raise ValueError("both gpu_temp and gpu_temp.missing exist; recover first")
                if sensor.exists():
                    sensor.rename(missing)
                elif not missing.exists():
                    raise ValueError("GPU sensor and backup are both missing; recover first")
            else:
                name, value = values[args.fault]
                if name == "gpu_temp" and (ROOT / "mock_hwmon/gpu_temp.missing").exists():
                    raise ValueError("GPU sensor is missing; recover before injecting heat")
                write_sensor(name, value)
            print(f"Injected {args.fault}; allow one sampling interval")
        elif args.command == "logs":
            with (ROOT / "runtime/events.log").open() as stream:
                print("".join(deque(stream, maxlen=10)), end="")
        else:
            data = state()
            if args.command == "status":
                print("AI Server BMC Simulator")
                print(f"Health   : {data['health']}")
                print(f"Recovery : {data['recovery_action']} (simulated recommendation)")
                print(f"Updated  : {data['timestamp']}")
            for label, key, unit in (
                    ("CPU", "cpu_temp_c", "C"), ("GPU", "gpu_temp_c", "C"),
                    ("FAN", "fan_rpm", "RPM"), ("PSU", "psu_voltage_v", "V")):
                value = data[key]
                print(f"{label:9}: {'unavailable' if value is None else f'{value} {unit}'}")
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"bmcctl: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
