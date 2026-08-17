# Makefile - copy changed files needed by MicroPython to esp32
#
# Roger B. Dannenberg
# March 2024

default: help

SHAREDFILES = o2lite.py o2lite_disc.py o2lite_handler.py \
              byte_swap.py ip_util.py __init__.py

PY3FILES = $(SHAREDFILES) py3discovery.py py3fns.py

UPYFILES = globals.py upydiscovery.py upyfns.py

# produce o2lite.srctime globals.upytime ...
TIMENAMESSRC = $(addsuffix .srctime,$(basename $(SHAREDFILES)))

TIMENAMESUPY = $(addsuffix .upytime,$(basename $(UPYFILES)))

# produce timestamps/o2lite.time timestamps/globals.time ...
TIMESTAMPS = $(addprefix timestamps/,$(TIMENAMESSRC)) \
	     $(addprefix timestamps/,$(TIMENAMESUPY))

# install files from src/ to lib/o2litepy/
timestamps/%.srctime : src/%.py
	@echo "src $<"
	@echo "target lib/o2litepy/$($<:src/%=%)"
	ampy --port $(SERIAL) put $< lib/o2litepy/$(subst src/,,$<)
	touch $@

timestamps/%.upytime : micropython/%.py
	@echo "src $<"
	@echo "target lib/o2litepy/$($<:micropython/%=%)"
	ampy --port $(SERIAL) put $< lib/o2litepy/$(subst micropython/,,$<)
	touch $@

# install globals as a library file
timestamps/global.time: micropython/globals.py
	ampy --port $(SERIAL) put micropython/globals.py lib/globals.py
	touch timestamps/global.time


# upload any changed files using ampy:
upload: $(TIMESTAMPS) timestamps/global.time


clean:
	rm -r timestamps/*

# build the python package from the root src/o2litepy layout
build:
	python -m build

help:
	@echo "make upload SERIAL=/dev/tty.usbserial-D306EBLS -- upload files to ESP32"
	@echo "make clean -- remove timestamps to force uploading everything"
	@echo "make build -- build a Python3 package in dist/"

