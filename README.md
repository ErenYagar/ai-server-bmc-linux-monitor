# AI Server BMC Telemetry & Fault Triage Simulator

Linux-based interview project for learning BMC firmware development workflow,
telemetry, fault handling and CI.

> This project is a learning simulator.
> It is NOT a production OpenBMC implementation.
> It is NOT a fully Redfish-conformant implementation.

## Executive summary

A small C11 daemon samples four file-backed sensors every second, evaluates
configured thresholds, records fault transitions and publishes an atomic JSON
snapshot. A Python CLI injects faults and displays telemetry. A loopback HTTP
facade exposes a read-only Redfish-inspired subset. Tests exercise normal and
failure paths; triage bundles preserve debugging evidence.

Built for discussing skills relevant to
鴻海 PP25121500002【AI Server】FW BMC Engineer. This is an independent learning
project, with no claim of employer affiliation, real board validation or
production readiness.

## Why this project

AI server management involves interpreting thermal, fan and power telemetry,
distinguishing hardware faults from bad sensor reads, and producing evidence
another engineer can reproduce. This project makes those decisions visible
without an OpenBMC/Yocto build or physical hardware.

The focus is reproducible Linux development: strict compiler flags, clear
interfaces, deterministic faults, graceful shutdown, testable recovery
recommendations and traceable Git history.

## Architecture

~~~mermaid
flowchart TD
    A["Mock sensors / future I2C-PMBus backend"] --> B["Linux C monitor daemon"]
    C["config/thresholds.conf"] --> B
    B --> D["Health / events / simulated recovery"]
    D --> E["Atomic state JSON"]
    D --> F["Append-only event log"]
    E --> G["Python CLI + Redfish-style read-only API"]
    F --> G
    E --> H["Diagnostic triage bundle"]
    F --> H
~~~

- **Sensor layer:** reads engineering-unit values from four mock files. Rejects
  missing/unreadable files, empty values, extra text, embedded NUL, overflow,
  NaN/infinity and values outside the simulator's plausibility bounds. Snapshot
  updates are all-or-nothing; failures publish all four readings as JSON null
  with sensors_valid=false.
- **Health engine:** pure evaluation with no mutable global state. Critical
  conditions precede warnings. Invalid inputs produce SENSOR_ERROR.
- **Daemon:** one writer per runtime directory, enforced with a Linux file lock.
  Monitoring state is local; a single volatile sig_atomic_t flag handles
  SIGINT/SIGTERM. Config/output errors terminate with diagnostics and nonzero
  exit. Sensor failures remain recoverable.
- **Events:** UTC ISO8601 timestamp, severity, event, value and recovery action.
  A changed fault set logs its current faults, including transitions that stay
  CRITICAL; an unchanged set does not append every second. Clearing all faults
  logs RECOVERED.
- **State:** temporary-file write plus rename prevents partial JSON reads.
  CLI/API reject snapshots older than five seconds. This is a freshness check,
  not a guarantee the daemon is alive at request time.

## Features and thresholds

[config/thresholds.conf](config/thresholds.conf) is loaded at startup. Restart
after editing. All five keys are required; duplicate/unknown keys, invalid
numbers and inconsistent limits fail startup.

| Input | Normal | Warning | Critical | Accepted input bounds |
|---|---:|---:|---:|---|
| CPU temperature | 55 °C | ≥ 80 °C | No separate CPU critical limit | −40 to 150 °C |
| GPU temperature | 65 °C | ≥ 80 °C | ≥ 95 °C | −40 to 150 °C |
| System fan | 3200 RPM | — | < 1000 RPM | Integer, 0 to 100000 RPM |
| PSU voltage | 12.1 V | — | < 11.4 V | 0 to 20 V |

These are illustrative thresholds and bounds, not vendor hardware limits.
Mock temperatures are already in degrees C and voltages in V; real backends
must convert their device/driver units.

| Fault | Health | Primary simulated recommendation |
|---|---|---|
| GPU overheat | CRITICAL | FAN_DEMAND_100_PERCENT |
| Fan below minimum | CRITICAL | HARDWARE_INSPECTION_REQUIRED |
| PSU undervoltage | CRITICAL | PSU_TRIAGE_REQUIRED |
| Sensor read/parse failure | SENSOR_ERROR | SENSOR_BUS_TRIAGE_REQUIRED |
| Warning only or normal | WARNING or OK | NONE |

Concurrent valid faults are all logged. The single recommendation prioritizes
fan inspection, then PSU triage, then thermal fan demand. Sensor failure takes
precedence because this prototype treats the entire snapshot as invalid.
The FAN_STALL event name also covers a fan below the minimum threshold.

**Recovery is a recommendation string only.** It does not change PWM, reset a
board, protect hardware or implement closed-loop fan control. The recover
command explicitly restores the mock files.

## Repository structure

~~~text
src/                    C daemon, health, sensor, event and state implementations
include/                C module interfaces
mock_hwmon/             cpu_temp, gpu_temp, fan_rpm, psu_voltage
config/thresholds.conf  Runtime thresholds
runtime/.gitkeep        Generated state, logs, lock and triage bundles are ignored
tools/bmcctl.py         Status, sensors, logs, fault injection and recovery
tools/redfish_mock.py   Loopback read-only HTTP facade
tests/test_monitor.c    Health logic and boundary assertions
tests/test_integration.py
                        Sensor parsing, config, lifecycle and HTTP contracts
scripts/reset_mock.sh   Restore normal values and a missing sensor
scripts/wait_state.py   Bounded polling used by regression/demo
scripts/regression.sh  Fault sequence with trap cleanup, then isolated tests
scripts/collect_triage.sh
                        Environment, revision, telemetry and reproduction evidence
scripts/demo.sh         Automated interview demonstration
packaging/bmc-monitor.service
                        Optional systemd user-service template
.github/workflows/ci.yml
                        Build, unit tests and regression on push/PR
Makefile
~~~

## Build

Validated locally on WSL 2 Ubuntu 24.04.4 LTS, GCC 13.3.0, GNU Make 4.3,
Python 3.12.3 and Git 2.43.0. Dependencies are libc, Bash, Python's standard
library and standard Linux utilities, including util-linux flock.

~~~bash
cd ~/ai-server-bmc-linux-monitor
make clean all
make test
make regression
~~~

No pip packages, Docker, virtual-machine images, Yocto build or package upgrades
are needed. Compiler flags include -std=c11 -Wall -Wextra -Werror -O2.
Binaries and runtime artifacts are excluded from Git.

Run one demo/regression per checkout and stop your manual daemon first.
**make clean deletes binaries, state, logs and all triage bundles.**
Copy evidence you want to retain outside runtime before cleaning.

## Run

Terminal A, from the repository root:

~~~bash
./scripts/reset_mock.sh
./bin/bmc-monitor
~~~

Terminal B:

~~~bash
cd ~/ai-server-bmc-linux-monitor
python3 tools/bmcctl.py status
python3 tools/bmcctl.py sensors
python3 tools/bmcctl.py logs
~~~

Press Ctrl+C in terminal A to stop. SIGTERM also exits cleanly. The daemon runs
in the foreground by design. An existing lock file is harmless: the kernel-held
lock, not the file's presence, prevents a second daemon.

## Fault injection

Allow one sampling interval after each command. Independent scenarios should
start with recover; injections otherwise accumulate.

~~~bash
python3 tools/bmcctl.py inject gpu-overheat    # GPU = 98.0 C
python3 tools/bmcctl.py recover
python3 tools/bmcctl.py inject fan-stall       # Fan = 0 RPM
python3 tools/bmcctl.py recover
python3 tools/bmcctl.py inject psu-undervolt    # PSU = 10.8 V
python3 tools/bmcctl.py recover
python3 tools/bmcctl.py inject sensor-missing  # Rename GPU file to .missing
python3 tools/bmcctl.py recover
~~~

Individual numeric writes use atomic replacement. Resetting four files is not
an atomic transaction, so a sample during reset may observe a transitional
combination. Tests poll for expected state rather than relying on a fixed
sleep. Sensor error does not replace bad input with fabricated zeroes.

## Redfish-style read-only API

With the C daemon running, start in another terminal:

~~~bash
python3 tools/redfish_mock.py
~~~

Then inspect all four implemented endpoints:

~~~bash
curl -fsS http://127.0.0.1:8000/redfish/v1
curl -fsS http://127.0.0.1:8000/redfish/v1/Chassis/1/Thermal
curl -fsS http://127.0.0.1:8000/redfish/v1/Chassis/1/Power
curl -fsS http://127.0.0.1:8000/redfish/v1/Managers/1/LogServices/EventLog/Entries
~~~

Responses use Content-Type: application/json. Unknown paths return JSON 404.
Missing, malformed or stale state returns JSON 503, including at the root
endpoint. Missing event logs return 503 for Entries. POST, PUT, PATCH and DELETE
return JSON 405. HEAD is supported; other unsupported methods return JSON
errors. Stop the facade with Ctrl+C.

The server binds only to 127.0.0.1:8000. It has no authentication, TLS, Redfish
schema validation, resource discovery graph or write/control operations.
SimulatorHealth exposes the simulator's own vocabulary. These names and paths
are inspired by Redfish; this is **not Redfish conformant**.

## Tests

~~~bash
make clean all
make test
make regression
~~~

Expected output includes **test_monitor: PASS** and **regression: PASS**.

- C assertions cover normal values, CPU/GPU warning, GPU critical, fan stall,
  PSU undervoltage, exact boundaries, precedence, NULL/non-finite inputs and
  health string conversion.
- Shell regression exercises GPU fault → recovery → fan fault → missing sensor
  → recovery → PSU fault → recovery, checking actions, events and liveness.
  EXIT/INT/TERM traps stop and reap the daemon and restore mock values.
- Eight Python integration tests use temporary fixtures for malformed sensors,
  missing/unreadable sensors, config validation, same-severity transitions,
  log deduplication, output failure, duplicate writers, SIGINT/SIGTERM and
  HTTP contracts. HTTP tests use an ephemeral loopback port and shut down
  their server thread.

No third-party Python dependencies. These are simulator tests, not hardware
qualification or Redfish conformance tests.

## CI/CD flow

~~~mermaid
flowchart TD
    A["Feature branch"] --> B["Git commit"]
    B --> C["Pull request"]
    C --> D["GitHub Actions"]
    D --> E["Build with warnings as errors"]
    E --> F["C unit tests"]
    F --> G["Fault regression + integration tests"]
    G --> H["Review and merge"]
~~~

BMC Linux CI runs on push and pull request with contents: read, the official
[actions/checkout@v7](https://github.com/actions/checkout) and Ubuntu runners.
No deployment is configured; CD here is reviewed merge, not automatic firmware
installation. CI failures must be reproduced and fixed before merge.
Build artifacts, logs and triage bundles are not committed.

## 3-minute interview demo

~~~bash
bash scripts/demo.sh
~~~

Commands finish in seconds, leaving time to explain:

| Time | Talk track |
|---|---|
| 0:00–0:30 | State the simulator boundary and thermal/fan/power problem |
| 0:30–1:00 | Show architecture and normal snapshot |
| 1:00–1:40 | Inject 98 °C GPU heat; explain CRITICAL and simulated fan demand |
| 1:40–2:15 | Show the event and captured triage evidence |
| 2:15–2:40 | Restore inputs and show OK/NONE; explain recoverability |
| 2:40–3:00 | Show tests/CI and describe the first real-board integration step |

The script rebuilds, tests, injects, captures evidence, recovers and reaps its
daemon. Success ends with **demo: PASS**. Evidence remains until the next clean.

## Troubleshooting and NPI triage

~~~bash
./scripts/collect_triage.sh
find runtime -maxdepth 2 -type f | sort
~~~

Each UTC-named runtime/triage-YYYYMMDD-HHMMSS directory contains:

| File | Evidence |
|---|---|
| environment.txt | Kernel, distro, GCC and Make versions |
| git-revision.txt | Exact HEAD and short working-tree status |
| sensor-state.json | Snapshot at capture, or explicit missing-state error |
| last-events.log | Last 50 event lines |
| reproduction.txt | Fault commands and expected outcomes |

A same-second capture adds a PID suffix to preserve both. Snapshot and events
may come from slightly different instants; this is not a synchronized system
dump. The reproduction file lists common scenarios, not recorded command history.

| Symptom | Next check |
|---|---|
| Config missing at startup | Run daemon from repository root |
| SENSOR_ERROR | Inspect missing file, permissions, invalid content; run recover |
| Missing/stale CLI/API state | Start daemon; inspect stderr and timestamp |
| “another daemon may be running” | Stop existing daemon before starting another |
| API bind fails | Check port 8000 and stop your earlier facade |
| Output write failure | Check runtime path, permissions and available disk |
| Regression fails | Read runtime/regression-daemon.log; preserve before cleaning |

## Mapping to real BMC/OpenBMC

These are learning analogies, not implemented OpenBMC components.

| Simulator component | Concept to explore on real hardware |
|---|---|
| mock_hwmon | Linux hwmon / I2C / PMBus sensor backend |
| C daemon | BMC monitoring/service concept |
| State JSON | Simplified stand-in for internal service state |
| Redfish mock | Simplified management API layer |
| Event log | BMC fault/event handling concept |
| collect_triage.sh | Customer/factory/NPI debugging concept |

The [OpenBMC project](https://github.com/openbmc/openbmc) is the reference point
for future platform integration. This repository includes none of its firmware
build output or service implementations.

## Optional systemd user service

WSL systemd was available during preflight. The template uses
%h/ai-server-bmc-linux-monitor as a **user service**, running as the user.
Adjust both paths if the repository is elsewhere.

Optional manual setup after stopping any manually launched daemon:

~~~bash
make all
mkdir -p ~/.config/systemd/user
cp packaging/bmc-monitor.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user start bmc-monitor
systemctl --user status bmc-monitor
journalctl --user -u bmc-monitor -n 30
systemctl --user stop bmc-monitor
~~~

No service is installed/enabled by setup or demo. Automatic start, user lingering
and WSL boot configuration are outside the MVP.

## Known limitations

- No real BMC hardware
- No OpenBMC image
- No D-Bus implementation
- No kernel driver
- No real I2C / PMBus device
- Redfish-style subset only
- Not Redfish conformant
- Not production firmware
- One CPU, GPU, fan and PSU channel; no inventory or hotplug model
- One-second sampling; no hysteresis, debounce, persistence or fan control
- No separate CPU critical policy
- All readings unavailable on any sensor failure
- No log rotation; Entries returns the entire log
- No concurrent injection coordination or transactional multi-sensor reset
- Atomic rename does not provide power-loss durability; no fsync
- Snapshot and events are separate writes, with no cross-file transaction
- No authentication/TLS, watchdog, secure update or hardware safety guarantee

## Future work

- Add real /dev/i2c-N backend
- Add Linux hwmon backend
- Integrate and validate the supplied systemd service on a target board
- Add D-Bus interface
- Port the service concept into OpenBMC
- Validate the API with official Redfish tooling
- Add a PLDM/MCTP learning module
- Derive board-specific limits and validate hysteresis/debounce with recordings

## Interview talking points

1. Distinguish failed sensor reads from real over-temperature.
2. Explain critical precedence and exact boundary tests.
3. Demonstrate that unchanged health can hide a changed fault.
4. Explain atomic JSON replacement and its durability limits.
5. Use triage revision, dirty status and event values to reproduce a failure.
6. Show local checks and the matching GitHub Actions results.
7. Describe replaceable interfaces without claiming real hardware support.

### 60 秒口說版本

「這個專題模擬 AI Server BMC 在溫度、風扇與電源異常時的監控和除錯流程。
我用 Linux C daemon 每秒讀取感測資料，把讀值、健康判斷、事件與 recovery
策略分開，再透過 JSON 提供 CLI 和唯讀的 Redfish-style API。

測試分成 C 單元測試、故障注入回歸，以及解析錯誤、訊號和 HTTP 整合測試，
並交給 GitHub Actions 重複驗證。GPU 過熱時會記錄 CRITICAL 和模擬風扇需求；
感測器遺失則標示 SENSOR_ERROR，避免把無效資料當正常值。診斷包保留版本、
環境和事件，方便重現。

它對應 BMC 監控與 NPI 除錯概念，但不是 OpenBMC 產品。拿到真實板子後，
我會先確認 sensor map、單位與 datasheet，再接 hwmon 或 I2C/PMBus backend，
量測真實資料，最後才驗證門檻、控制策略與平台整合。」
