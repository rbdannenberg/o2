# Makefile - copy changed files needed by MicroPython to esp32
#
# Roger B. Dannenberg
# March 2024

default: help

SHAREDFILES = o2lite.py o2lite_disc.py o2lite_handler.py \
              byte_swap.py ip_util.py __init__.py

PY3FILES = $(SHAREDFILES) py3discovery.py py3fns.py

UPYFILES = $(SHAREDFILES) globals.py upydiscovery.py upyfns.py

# produce o2lite.time globals.time ...
TIMENAMES = $(addsuffix .time,$(basename $(UPYFILES)))

# produce timestamps/o2lite.time timestamps/globals.time ...
TIMESTAMPS = $(addprefix timestamps/,$(TIMENAMES))

# install files from src/ to lib/o2litepy/
timestamps/%.time : src/%.py
	@echo "src $<"
	@echo "target lib/o2litepy/$($<:src/%=%)"
	ampy --port $(SERIAL) put $< lib/o2litepy/$(subst src/,,$<)
	touch $@

# install globals as a library file
timestamps/global.time: src/globals.py
	ampy --port $(SERIAL) put src/globals.py lib/globals.py
	touch timestamps/global.time


# upload any changed files using ampy:
upload: $(TIMESTAMPS) timestamps/global.time


clean:
	rm -r timestamps/*

# build the python package by copying files to py3pkg in case
# they are updated, then run build
build:
	$(foreach SRC,$(PY3FILES),cp src/$(SRC) py3pkg/src/o2litepy/$(SRC) ;)
	cd py3pkg; python -m build

help:
	@echo "make SERIAL=/dev/tty.usbserial-D306EBLS -- upload files to ESP32"
	@echo "make clean -- remove timestamps to force uploading everything"
	@echo "make build -- build a Python3 package in py3pkg"

