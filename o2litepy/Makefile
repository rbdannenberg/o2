# Makefile - copy changed files needed by MicroPython to esp32
#
# Roger B. Dannenberg
# March 2024

default: upload

SHAREDFILES = o2lite.py o2lite_disc.py o2lite_handler.py \
              byte_swap.py ip_util.py

PY3FILES = $(SHAREDFILES) py3discovery.py py3fns.py __init__.py

UPYFILES = $(SHAREDFILES) globals.py upydiscovery.py upyfns.py

# produce o2lite.time globals.time ...
TIMENAMES = $(addsuffix .time,$(basename $(UPYFILES)))

# produce timestamps/o2lite.time timestamps/globals.time ...
TIMESTAMPS = $(addprefix timestamps/,$(TIMENAMES))

timestamps/%.time : src/%.py
	ampy --port $(SERIAL) put $< $<
	touch $@

# upload any changed files using ampy:
upload: $(TIMESTAMPS)


clean:
	rm -f timestamps/*

# build the python package by copying files to py3pkg in case
# they are updated, then run build
build:
	$(foreach SRC,$(PY3FILES),cp src/$(SRC) py3pkg/src/o2litepy/$(SRC) ;)
	cd py3pkg; python -m build

help:
	@echo "make SERIAL=/dev/tty.usbserial-D306EBLS -- upload files to ESP32"
	@echo "make clean -- remove timestamps to force uploading everything"
	@echo "make build -- build a Python3 package in py3pkg"

