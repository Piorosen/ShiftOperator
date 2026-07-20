FQBN   ?= arduino:mbed_nano:nanorp2040connect
PORT   ?= /dev/cu.usbmodem6301
BAUD   ?= 9600
SKETCH ?= hello_world

.PHONY: build deploy serial

build:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

deploy: build
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

serial:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)
