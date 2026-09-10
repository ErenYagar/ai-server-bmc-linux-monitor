CC ?= gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -Iinclude
BIN_DIR := bin
COMMON_SRC := src/monitor.c src/sensor_mock.c src/event_log.c src/state.c
DAEMON_SRC := src/daemon.c $(COMMON_SRC)
HEADERS := $(wildcard include/*.h)

.PHONY: all clean test regression
all: $(BIN_DIR)/bmc-monitor

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/bmc-monitor: $(DAEMON_SRC) $(HEADERS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(DAEMON_SRC) -o $@

$(BIN_DIR)/test-monitor: tests/test_monitor.c src/monitor.c include/monitor.h | $(BIN_DIR)
	$(CC) $(CFLAGS) tests/test_monitor.c src/monitor.c -o $@

test: $(BIN_DIR)/test-monitor
	./$(BIN_DIR)/test-monitor

regression: all test
	bash scripts/regression.sh

clean:
	rm -rf -- $(BIN_DIR) runtime/*.json runtime/*.log runtime/*.tmp runtime/triage-*
