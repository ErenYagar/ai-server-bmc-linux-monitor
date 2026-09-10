#!/usr/bin/env python3
"""Loopback-only Redfish-inspired read-only subset, NOT Redfish conformant."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
PATHS = (
    "/redfish/v1",
    "/redfish/v1/Chassis/1/Thermal",
    "/redfish/v1/Chassis/1/Power",
    "/redfish/v1/Managers/1/LogServices/EventLog/Entries",
)


def reject_constant(value):
    raise ValueError(f"invalid JSON number: {value}")


def read_state():
    data = json.loads((ROOT / "runtime/state.json").read_text(),
                      parse_constant=reject_constant)
    if not isinstance(data, dict) or data["health"] not in (
            "OK", "WARNING", "CRITICAL", "SENSOR_ERROR"):
        raise ValueError("invalid state health")
    if not isinstance(data["recovery_action"], str):
        raise ValueError("invalid recovery action")
    valid = data["sensors_valid"]
    if type(valid) is not bool or valid == (data["health"] == "SENSOR_ERROR"):
        raise ValueError("inconsistent sensor validity")
    for name in ("cpu_temp_c", "gpu_temp_c", "fan_rpm", "psu_voltage_v"):
        value = data[name]
        if (valid and (type(value) not in (int, float) or
                       not -float("inf") < value < float("inf"))) or (
                not valid and value is not None):
            raise ValueError("invalid sensor reading")
    stamp = datetime.fromisoformat(data["timestamp"])
    age = (datetime.now(timezone.utc) - stamp).total_seconds()
    if age < -1 or age > 5:
        raise ValueError("state is stale; start the monitor daemon")
    return data


class Handler(BaseHTTPRequestHandler):
    def respond(self, status, body):
        payload = json.dumps(body, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError):
                pass  # A disconnected client must not affect the server.

    def send_error(self, code, message=None, explain=None):
        self.respond(code, {"error": {"code": code, "message": message or "Request failed"}})

    def do_GET(self):
        path = self.path.rstrip("/")
        if path not in PATHS:
            self.send_error(404, "Unknown simulator endpoint")
            return
        try:
            s = read_state()
            common = {"Simulator": True, "SimulatorHealth": s["health"],
                      "Timestamp": s["timestamp"],
                      "RecoveryAction": s["recovery_action"]}
            if path == PATHS[0]:
                body = {"Name": "BMC learning simulator",
                        "Description": "Redfish-inspired subset; NOT Redfish conformant",
                        "Endpoints": list(PATHS[1:]), **common}
            elif path == PATHS[1]:
                body = {"Temperatures": [
                            {"Name": "CPU", "ReadingCelsius": s["cpu_temp_c"]},
                            {"Name": "GPU", "ReadingCelsius": s["gpu_temp_c"]}],
                        "Fans": [{"Name": "System fan", "Reading": s["fan_rpm"],
                                  "ReadingUnits": "RPM"}], **common}
            elif path == PATHS[2]:
                body = {"Voltages": [{"Name": "PSU", "ReadingVolts": s["psu_voltage_v"]}],
                        **common}
            else:
                entries = []
                with (ROOT / "runtime/events.log").open() as stream:
                    for i, line in enumerate(stream, 1):
                        entries.append({"Id": str(i), "Message": line.rstrip()})
                body = {"Members": entries, "Members@odata.count": len(entries), **common}
            self.respond(200, body)
        except (OSError, ValueError, KeyError, TypeError, OverflowError) as exc:
            self.send_error(503, f"Telemetry unavailable: {exc}")

    do_HEAD = do_GET

    def read_only(self):
        self.send_error(405, "This simulator API is read-only")

    do_POST = do_PUT = do_PATCH = do_DELETE = read_only


def main():
    try:
        with ThreadingHTTPServer(("127.0.0.1", 8000), Handler) as server:
            print("redfish-mock: http://127.0.0.1:8000 (learning subset only)", flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        return 0
    except OSError as exc:
        print(f"redfish-mock: {exc}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
