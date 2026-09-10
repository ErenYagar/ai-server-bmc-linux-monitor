#!/usr/bin/env python3
"""Isolated backend, lifecycle and HTTP checks; no pip dependencies."""
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "bin/bmc-monitor"


class Integration(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="bmc-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for folder in ("config", "mock_hwmon"):
            shutil.copytree(ROOT / folder, self.root / folder)
        (self.root / "runtime").mkdir()

    def sensor(self, name, value):
        target = self.root / "mock_hwmon" / name
        temp = target.with_suffix(".tmp")
        temp.write_text(value)
        temp.replace(target)

    @contextmanager
    def daemon(self):
        with (self.root / "runtime/test-daemon.log").open("w+") as log:
            process = subprocess.Popen([str(BINARY)], cwd=self.root,
                                       stdout=log, stderr=log)
            try:
                yield process
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    code = process.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    self.fail("daemon ignored SIGTERM")
                if code != 0:
                    log.seek(0)
                    self.fail(f"daemon exit={code}: {log.read()}")

    def wait_state(self, health, action=None):
        deadline = time.monotonic() + 6
        last = "no state"
        while time.monotonic() < deadline:
            try:
                last = json.loads((self.root / "runtime/state.json").read_text())
                if last["health"] == health and (
                        action is None or last["recovery_action"] == action):
                    return last
            except (OSError, ValueError) as exc:
                last = str(exc)
            time.sleep(0.03)
        self.fail(f"expected {health}/{action}, last={last}")

    def test_sensor_input_validation(self):
        cases = [
            ("gpu_temp", b""), ("gpu_temp", b"oops\n"),
            ("gpu_temp", b"65.0junk\n"), ("gpu_temp", b"65\njunk"),
            ("gpu_temp", b"nan\n"), ("gpu_temp", b"inf\n"),
            ("gpu_temp", b"1e999\n"), ("gpu_temp", b"151\n"),
            ("gpu_temp", b"-41\n"), ("gpu_temp", b"65\x00junk"),
            ("gpu_temp", b"6" * 200),
            ("fan_rpm", b"3200.5\n"), ("fan_rpm", b"-1\n"),
            ("fan_rpm", b"9999999999999999999999\n"),
            ("psu_voltage", b"-0.1\n"), ("psu_voltage", b"21\n"),
        ]
        for name, content in cases:
            with self.subTest(sensor=name, value=content[:24]):
                target = self.root / "mock_hwmon" / name
                original = target.read_bytes()
                target.write_bytes(content)
                state = self.root / "runtime/state.json"
                state.unlink(missing_ok=True)
                with self.daemon() as process:
                    result = self.wait_state("SENSOR_ERROR", "SENSOR_BUS_TRIAGE_REQUIRED")
                    self.assertIsNone(process.poll())
                    self.assertFalse(result["sensors_valid"])
                    for key in ("cpu_temp_c", "gpu_temp_c", "fan_rpm", "psu_voltage_v"):
                        self.assertIsNone(result[key])
                target.write_bytes(original)

    def test_missing_and_unreadable_sensor(self):
        target = self.root / "mock_hwmon/gpu_temp"
        backup = target.with_suffix(".missing")
        target.rename(backup)
        with self.daemon() as process:
            self.wait_state("SENSOR_ERROR")
            backup.rename(target)
            self.wait_state("OK")
            target.unlink()
            target.mkdir()
            self.wait_state("SENSOR_ERROR")
            target.rmdir()
            self.sensor("gpu_temp", " 65.0 \n\t")
            self.wait_state("OK")
            self.assertIsNone(process.poll())

    def test_fault_transitions_and_deduplication(self):
        self.sensor("gpu_temp", "98\n")
        with self.daemon():
            self.wait_state("CRITICAL", "FAN_DEMAND_100_PERCENT")
            self.sensor("fan_rpm", "0\n")
            self.wait_state("CRITICAL", "HARDWARE_INSPECTION_REQUIRED")
            log = self.root / "runtime/events.log"
            observed = log.read_text()
            self.assertIn("event=GPU_OVERHEAT value=98.00", observed)
            self.assertIn("event=FAN_STALL value=0.00", observed)
            time.sleep(1.2)
            self.assertEqual(log.read_text(), observed, "unchanged faults must not flood log")
            self.sensor("gpu_temp", "65\n")
            self.sensor("fan_rpm", "3200\n")
            self.wait_state("OK", "NONE")
            self.assertIn("event=RECOVERED", log.read_text())

    def test_snapshot_preserves_boundary_precision(self):
        self.sensor("psu_voltage", "11.399\n")
        with self.daemon():
            data = self.wait_state("CRITICAL", "PSU_TRIAGE_REQUIRED")
            self.assertEqual(data["psu_voltage_v"], 11.399)

    def test_signals_and_single_writer(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=signum):
                (self.root / "runtime/state.json").unlink(missing_ok=True)
                with self.daemon() as process:
                    self.wait_state("OK")
                    duplicate = subprocess.run([str(BINARY)], cwd=self.root,
                                               capture_output=True, text=True, timeout=4)
                    self.assertNotEqual(duplicate.returncode, 0)
                    self.assertIn("another daemon", duplicate.stderr)
                    process.send_signal(signum)
                    self.assertEqual(process.wait(timeout=3), 0)

    def test_configuration_validation(self):
        path = self.root / "config/thresholds.conf"
        original = path.read_text()
        cases = [
            original.replace("gpu_crit_c=95.0", "gpu_crit_c=70.0"),
            original.replace("fan_min_rpm=1000", "fan_min_rpm=1000.5"),
            original.replace("psu_min_v=11.4", "psu_min_v=nan"),
            original.replace("cpu_warn_c=80.0\n", ""),
            original + "gpu_crit_c=99\n",
            original + "unexpected=1\n",
            original + "bad line\n",
        ]
        for content in cases:
            with self.subTest(config=content):
                path.write_text(content)
                result = subprocess.run([str(BINARY)], cwd=self.root,
                                        capture_output=True, text=True, timeout=4)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("load config/thresholds.conf", result.stderr)
        path.write_text(original.replace("gpu_warn_c=80.0", "gpu_warn_c=60.0"))
        with self.daemon():
            self.wait_state("WARNING")

    def test_output_failure_is_reported(self):
        for path in ("events.log", "state.json.tmp"):
            with self.subTest(path=path):
                target = self.root / "runtime" / path
                target.unlink(missing_ok=True)
                target.mkdir()
                result = subprocess.run([str(BINARY)], cwd=self.root,
                                        capture_output=True, text=True, timeout=4)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("write runtime/", result.stderr)
                target.rmdir()

    def test_http_contract(self):
        spec = importlib.util.spec_from_file_location("redfish_test",
                                                     ROOT / "tools/redfish_mock.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = self.root
        module.Handler.log_message = lambda *args: None
        server = module.ThreadingHTTPServer(("127.0.0.1", 0), module.Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()

        def request(path, expected=200, method="GET"):
            req = Request(f"http://127.0.0.1:{server.server_port}{path}", method=method)
            try:
                response = urlopen(req, timeout=3)
            except HTTPError as exc:
                response = exc
            with response:
                self.assertEqual(response.status, expected)
                self.assertEqual(response.headers.get_content_type(), "application/json")
                payload = response.read()
                return json.loads(payload) if payload else None

        try:
            request("/redfish/v1", 503)
            request("/unknown", 404)
            with self.daemon():
                self.wait_state("OK")
                for path in module.PATHS:
                    body = request(path)
                    self.assertTrue(body["Simulator"])
                    self.assertEqual(body["SimulatorHealth"], "OK")
                thermal = request(module.PATHS[1])
                self.assertEqual(thermal["Temperatures"][1]["ReadingCelsius"], 65.0)
                self.assertEqual(request(module.PATHS[2])["Voltages"][0]["ReadingVolts"], 12.1)
                self.assertIsNone(request(module.PATHS[0], method="HEAD"))
                for method in ("POST", "PUT", "PATCH", "DELETE"):
                    request(module.PATHS[0], 405, method)
                saved = (self.root / "runtime/state.json").read_text()
            state = self.root / "runtime/state.json"
            for invalid in ("{", "[]", "null", '{"health": "OK"}',
                            json.dumps({**json.loads(saved), "cpu_temp_c": float("nan")}),
                            saved.replace('"sensors_valid": true', '"sensors_valid": false')):
                state.write_text(invalid)
                request(module.PATHS[0], 503)
            stale = json.loads(saved)
            stale["timestamp"] = (datetime.now(timezone.utc) -
                                  timedelta(seconds=30)).isoformat()
            state.write_text(json.dumps(stale))
            request(module.PATHS[0], 503)
            missing = json.loads(saved)
            missing.update(health="SENSOR_ERROR", sensors_valid=False,
                           recovery_action="SENSOR_BUS_TRIAGE_REQUIRED",
                           timestamp=datetime.now(timezone.utc).isoformat())
            for key in ("cpu_temp_c", "gpu_temp_c", "fan_rpm", "psu_voltage_v"):
                missing[key] = None
            state.write_text(json.dumps(missing))
            self.assertIsNone(request(module.PATHS[1])["Temperatures"][0]["ReadingCelsius"])
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()
            self.assertFalse(thread.is_alive(), "HTTP test thread leaked")


if __name__ == "__main__":
    unittest.main(verbosity=2)
