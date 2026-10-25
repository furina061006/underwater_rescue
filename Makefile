# underwater_rescue build / test / checksum entry points.
CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
PYTHON ?= python3
MAVLINK_INC ?= /opt/ros/noetic/include
MAVLINK_HEADER := $(MAVLINK_INC)/mavlink/v2.0/ardupilotmega/mavlink.h
CONTROL := bin/rescue_control
CONTROL_SRC := src/control/rescue_control.cpp
CONTROL_HDR := src/control/pillar_ram_logic.hpp
TEST_SRCS := $(wildcard tests/cpp/*.cpp)
TEST_BINS := $(patsubst tests/cpp/%.cpp,build/tests/%,$(TEST_SRCS))

.PHONY: all control control-check preview run test test-cpp test-python checksums clean help

all: control

# The flight controller build needs ArduPilot MAVLink headers. ROS Noetic ships
# them under /opt/ros/noetic/include; override with MAVLINK_INC=/path/to/include.
control:
	@test -f "$(MAVLINK_HEADER)" || { \
		echo "ERROR: MAVLink header not found: $(MAVLINK_HEADER)"; \
		echo "Set MAVLINK_INC to the directory containing mavlink/v2.0/."; \
		exit 1; \
	}
	install -d bin
	$(CXX) $(CXXFLAGS) -I$(MAVLINK_INC) -Isrc/control $(CONTROL_SRC) -o $(CONTROL)
	@echo "built $(CONTROL)"

# Build a controller for validation without touching bin/rescue_control.
control-check:
	@test -f "$(MAVLINK_HEADER)" || { \
		echo "ERROR: MAVLink header not found: $(MAVLINK_HEADER)"; \
		exit 1; \
	}
	install -d build
	$(CXX) $(CXXFLAGS) -I$(MAVLINK_INC) -Isrc/control $(CONTROL_SRC) -o build/rescue_control
	@echo "built build/rescue_control"

# Logic-only tests: no MAVLink, camera or hardware required.
$(TEST_BINS): build/tests/%: tests/cpp/%.cpp $(CONTROL_HDR)
	install -d build/tests
	$(CXX) $(CXXFLAGS) -Isrc/control $< -o $@

test-cpp: $(TEST_BINS)
	@set -e; \
	for t in $(TEST_BINS); do \
		echo "== $$t"; \
		./$$t; \
	done; \
	echo "C++ LOGIC TESTS PASS"

# Vision tests need OpenCV + NumPy (installed on the target).
test-python:
	@set -e; \
	for t in tests/python/test_*.py; do \
		echo "== $$t"; \
		PYTHONPATH=src/vision $(PYTHON) $$t; \
	done; \
	echo "PYTHON VISION TESTS PASS"

test: test-cpp test-python

preview:
	$(PYTHON) run_rescue.py

run:
	$(PYTHON) run_rescue.py --run

checksums:
	@find . -path ./.git -prune -o -path ./runtime -prune -o -path ./build -prune \
		-o -path '*/__pycache__' -prune -o -type f \
		! -name SHA256SUMS.txt ! -name '*.pyc' -print0 \
		| LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS.txt
	@echo "wrote SHA256SUMS.txt ($$(wc -l < SHA256SUMS.txt) files)"

# Keep runtime/ untouched: it may hold the only recovery record and flight logs.
clean:
	rm -rf build/tests build/rescue_control
	rm -f $(CONTROL)
	@echo "removed build/tests, build/rescue_control and $(CONTROL); runtime/ preserved"

help:
	@echo "make control        build bin/rescue_control (needs MAVLink headers)"
	@echo "make control-check  build build/rescue_control without touching bin/"
	@echo "make test-cpp       build and run pure logic tests"
	@echo "make test           also run OpenCV/NumPy vision tests"
	@echo "make preview        front-camera preview + tuning GUI"
	@echo "make run            preview + 290 s CH10 rescue run (hardware live)"
	@echo "make checksums      regenerate SHA256SUMS.txt for the source tree"
	@echo "make clean          remove built tests/controller, keep runtime/"
