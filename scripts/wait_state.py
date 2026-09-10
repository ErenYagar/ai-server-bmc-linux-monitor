#!/usr/bin/env python3
"""Bounded polling for demo/regression; fails with the last observed state."""
import json
from pathlib import Path
import sys
import time

path = Path(__file__).resolve().parent.parent / "runtime/state.json"
deadline = time.monotonic() + 7
last = "no state"
while time.monotonic() < deadline:
    try:
        data = json.loads(path.read_text())
        last = data
        if data["health"] == sys.argv[1] and data["recovery_action"] == sys.argv[2]:
            raise SystemExit(0)
    except (OSError, ValueError, KeyError) as exc:
        last = str(exc)
    time.sleep(0.1)
print(f"wait_state: expected {sys.argv[1:3]}, observed {last}", file=sys.stderr)
raise SystemExit(1)
