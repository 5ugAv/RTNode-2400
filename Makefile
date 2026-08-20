# ============================================================================
# WARNING: /dev/ttyACM0 IS HARDCODED IN ~30 UPLOAD TARGETS IN THIS FILE.
#
# That is safe on a bench machine with one board attached. It is NOT safe on a
# Node Medic, where /dev/ttyACM0 is the medic's OWN permanently-attached radio.
# Running any of those targets there aims a flash — or an rnodeconf WRITE — at
# the tool's own radio. This project has already flashed its own radio once by
# picking a port positionally.
#
# `upload-techo` has been converted to require an explicit PORT (see below).
# The rest have NOT been converted, because they are untested here. Before
# running any other upload-* target on a medic, pass the port by hand and use a
# /dev/serial/by-id/ path, which is stable across reboots where ttyACM* is not.
#
# Audited 2026-08-18.
# ============================================================================

# Copyright (C) 2024, Mark Qvist

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Version 2.0.17 of the Arduino ESP core is based on ESP-IDF v4.4.7
ARDUINO_ESP_CORE_VER = 2.0.17

# Version 3.2.0 of the Arduino ESP core is based on ESP-IDF v5.4.1
# ARDUINO_ESP_CORE_VER = 3.2.0

all: release

clean:
	-rm -r ./build
	-rm ./Release/rnode_firmware*

prep: prep-avr prep-esp32 prep-samd

prep-avr:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install arduino:avr --config-file arduino-cli.yaml
	arduino-cli core install unsignedio:avr --config-file arduino-cli.yaml

prep-esp32:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install esp32:esp32@$(ARDUINO_ESP_CORE_VER) --config-file arduino-cli.yaml
	arduino-cli lib install "Adafruit SSD1306"
	arduino-cli lib install "Adafruit SH110X"
	arduino-cli lib install "Adafruit ST7735 and ST7789 Library"
	arduino-cli lib install "Adafruit NeoPixel"
	arduino-cli lib install "XPowersLib"
	arduino-cli lib install "Crypto"

prep-samd:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install adafruit:samd --config-file arduino-cli.yaml

prep-nrf:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install rakwireless:nrf52 --config-file arduino-cli.yaml
	arduino-cli core install Heltec_nRF52:Heltec_nRF52 --config-file arduino-cli.yaml
	arduino-cli core install adafruit:nrf52 --config-file arduino-cli.yaml
	arduino-cli lib install "GxEPD2"
	arduino-cli lib install "ArduinoJson"
	arduino-cli lib install "MsgPack"
	arduino-cli config set library.enable_unsafe_install true
	arduino-cli lib install --git-url https://github.com/liamcottle/esp8266-oled-ssd1306#e16cee124fe26490cb14880c679321ad8ac89c95
	pip install adafruit-nrfutil --upgrade

console-site:
	make -C Console clean site

spiffs: console-site spiffs-image 

spiffs-image:
	python Release/esptool/spiffsgen.py 1966080 ./Console/build Release/console_image.bin

upload-spiffs:
	@echo Deploying SPIFFS image...
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

check_bt_buffers:
	@./esp32_btbufs.py ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/libraries/BluetoothSerial/src/BluetoothSerial.cpp

firmware:
	arduino-cli compile --log --fqbn unsignedio:avr:rnode

firmware-mega2560:
	arduino-cli compile --log --fqbn arduino:avr:mega

firmware-tbeam: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\""

firmware-tbeam_sx126x: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\" \"-DMODEM=0x03\""

firmware-t3s3:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x03\""

firmware-t3s3_sx127x:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x01\""

firmware-t3s3_sx1280_pa:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x04\""

firmware-tdeck:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3B\""

firmware-tbeam_supreme:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x3D"

firmware-lora32_v10: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\""

firmware-lora32_v10_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v20: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v21: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\""

firmware-lora32_v21_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v21_tcxo: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DENABLE_TCXO=true\""

firmware-heltec32_v2: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\""

firmware-heltec32_v2_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\" \"-DEXTERNAL_LEDS=true\""

firmware-heltec32_v3:
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V3 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3A\""

firmware-heltec32_v4:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3F\""

firmware-rnode_ng_20: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x40\""

firmware-rnode_ng_21: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x41\""

firmware-featheresp32: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:featheresp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x34\""

firmware-genericesp32: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:esp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x35\""

firmware-rak4631:
	arduino-cli compile --log --fqbn rakwireless:nrf52:WisCoreRAK4631Board -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x51\""

firmware-heltec_t114:
	arduino-cli compile --log --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3C\""

# LilyGO T-Echo / T-Echo Plus (nRF52840 + SX1262 + 1.54" e-paper), BOARD_MODEL 0x44.
#
# This target used to pass ONLY -DBOARD_MODEL, which quietly built a plain RNode
# rather than an RTNode: without -DHAS_RNS the C++ Reticulum stack is compiled
# out entirely, and the build then fails on VERBOSEF (lib/microReticulum/Log.h)
# because two call sites in the .ino are unguarded. Verified 2026-08-18.
#
# Three things arduino-cli needs that PlatformIO does for free:
#   --library            lib/ is NOT on the include path here, unlike PlatformIO's
#                        lib_dir, so microReticulum must be named explicitly.
#   -fexceptions         the Adafruit core compiles -fno-exceptions; microReticulum
#                        throws std::runtime_error. extra_flags land after the core's
#                        flags, so this wins.
#   -lstdc++ -lsupc++    the core also links -nostdlib with nano.specs, so nothing
#                        provides the exception runtime and the link fails with 79
#                        undefined references. compiler.libraries.ldflags is empty
#                        upstream, which makes it the clean hook.
#
# Result on 2026-08-18: 624520 bytes flash (76% of 815104), 70324 bytes static RAM
# (29% of 237568). Compiles and links; NOT yet run on hardware.
#
# Result on 2026-08-18, after adding the EpdGlyph.h status-glyph state
# machine + T-Echo integration (Display.h/Utilities.h/RNode_Firmware.ino):
# 630528 bytes flash (77% of 815104, +6008 bytes), 74484 bytes static RAM
# (31% of 237568, +4160 bytes). Compiles and links, zero warnings from any
# touched file. Still NOT yet run on hardware.
# Bring-up variant: pins the TLSF pool to 32KB instead of 80% of free heap, and
# builds with CFG_DEBUG=1 so vApplicationStackOverflowHook actually logs and hangs
# instead of silently returning (it is a no-op in Release). Use this for the FIRST
# boot on new hardware, then fall back to firmware-techo once it is known good.
# arduino-cli insists the sketch FOLDER be named after the .ino, and this tree
# is checked out as RTNode-2400 while its sketch is RNode_Firmware.ino. That
# mismatch is why copies of this tree kept appearing (techo-test/, tb2/,
# overlay_test/ ...), each drifting from the others. A symlink costs nothing and
# keeps ONE tree: the link points at whatever directory this Makefile is in, so
# it follows a rename, a worktree, or a clone without being edited.
# SEPARATE OUTPUT DIRS, and not for tidiness. arduino-cli does NOT put a menu
# option (debug=l1) in the export path, so firmware-techo and
# firmware-techo-bringup both landed in build/adafruit.nrf52.pca10056/ and the
# second silently overwrote the first. Two images that behave differently, one
# filename, nothing on disk saying which is which -- and the flasher takes a
# path. On 2026-08-19 the only way to tell them apart after the fact was that
# the debug build is 24KB bigger.
SKETCH_LINK := $(HOME)/.build-sketch/RNode_Firmware

.PHONY: sketch-link
sketch-link:
	@mkdir -p $(dir $(SKETCH_LINK))
	@ln -sfn $(CURDIR) $(SKETCH_LINK)

# THE BOARD MUST NAME ITSELF. Built plain, the pca10056 fqbn ships Nordic's
# USB strings ("nRF52840 DK"), and Node Medic identifies nRF52 boards by their
# USB product string -- so a running T-Echo RTNode was unidentifiable and the
# BIRTH gates stayed shut (2026-08-19). Overriding the board properties gives
# it back its true name; "T-Echo" in the product string is the needle the
# medic's detector matches, and the by-id symlink stays matchable by *T-Echo*.
USB_ID := --build-property 'build.usb_manufacturer="LilyGO"' --build-property 'build.usb_product="T-Echo RTNode-2400"'

firmware-techo-bringup: sketch-link
	cd $(SKETCH_LINK) && arduino-cli compile --log --fqbn adafruit:nrf52:pca10056:debug=l1 $(USB_ID) --output-dir $(CURDIR)/build/techo-bringup \
	  --library lib/microReticulum \
	  --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x44 -DHAS_RNS -DRNS_USE_FS -DRNS_PERSIST_PATHS -DRNS_USE_TLSF=1 -DRNS_USE_ALLOCATOR=1 -DRNS_TLSF_FIXED_SIZE=32768 -fexceptions" \
	  --build-property "compiler.libraries.ldflags=-lstdc++ -lsupc++" \
	  .

firmware-techo: sketch-link
	cd $(SKETCH_LINK) && arduino-cli compile --log --fqbn adafruit:nrf52:pca10056 $(USB_ID) --output-dir $(CURDIR)/build/techo-release \
	  --library lib/microReticulum \
	  --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x44 -DHAS_RNS -DRNS_USE_FS -DRNS_PERSIST_PATHS -DRNS_USE_TLSF=1 -DRNS_USE_ALLOCATOR=1 -DRNS_TLSF_FIXED_SIZE=32768 -fexceptions" \
	  --build-property "compiler.libraries.ldflags=-lstdc++ -lsupc++" \
	  .

# Off-device, native g++ contract test for EpdGlyph.h (the T-Echo status-
# glyph state machine + renderer) — no Arduino toolchain needed. Same
# precedent as reticulum-tool/tests/test_firmware_beacon_contract.py
# compiling HealthBeaconPack.h standalone.
test-epd-glyph:
	g++ -std=c++11 -Wall -Wextra -Itests/.. -o /tmp/test_epd_glyph tests/test_epd_glyph_native.cpp
	/tmp/test_epd_glyph

firmware-xiao_s3:
	arduino-cli compile --log --fqbn "esp32:esp32:XIAO_ESP32S3" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3E\""

upload:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn unsignedio:avr:rnode

upload-mega2560:
	arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:mega

upload-tbeam:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:t-beam
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.t-beam/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tbeam_sx1262:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:t-beam
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.t-beam/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v10:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v20:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v21:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v2:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:heltec_wifi_lora_32_V2
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB1 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v3:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:heltec_wifi_lora_32_V3
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v4:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tdeck:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tbeam_supreme:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rnode_ng_20:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rnode_ng_21:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-t3s3:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-featheresp32:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:featheresp32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.featheresp32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rak4631:
	arduino-cli upload -p /dev/ttyACM0 --fqbn rakwireless:nrf52:WisCoreRAK4631Board
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes from_device /dev/ttyACM0)

upload-heltec_t114:
	arduino-cli upload -p /dev/ttyACM0 --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes from_device /dev/ttyACM0)

# PORT IS NOT HARDCODED. /dev/ttyACM0 on a Node Medic is the medic's OWN radio
# (a Heltec Wireless Tracker); this target once pointed straight at it. Pass the
# port explicitly, and prefer a /dev/serial/by-id/ path, which is stable across
# reboots where ttyACM* ordering is not:
#   make upload-techo PORT=/dev/serial/by-id/usb-Nordic_NRF52_DK_<serial>-if00
PORT ?= UNSET
# Flash a built T-Echo image over serial DFU (board must be in the bootloader --
# double-tap RESET). VARIANT picks which image: release or bringup.
#
# THE GUARD IS THE POINT. This medic carries its own radio, JONESEY, permanently
# attached, and a naive "first port" pick has already tried to flash it once
# (2026-08-05). So the target is found by its LilyGo by-id name, the medic's own
# Espressif is resolved at the same moment, and the flash is refused if they turn
# out to be the same node. A ttyACM number is not an identity; a by-id name is.
VARIANT ?= release
# ── RAK4631 as an RTNode-2400 ────────────────────────────────────────────────
# Same nRF52840 + SX1262 family as the T-Echo, so the whole techo recipe
# applies: arduino-cli (every pio nRF env fails on auto-prototypes), the
# noalloc flag set (RNS_USE_ALLOCATOR crashes pre-main on this core), the
# sketch link, per-target output dirs, and the self-naming USB identity the
# medic's detector keys on. Builds on the RAK BSP (rakwireless:nrf52),
# installed with the native-ARM64 toolchain trick — the BSP publishes no
# aarch64 host tools, but the platform files are host-independent and the
# tool deps are symlinked to the adafruit-native ones.
USB_ID_RAK := --build-property 'build.usb_manufacturer="RAKwireless"' --build-property 'build.usb_product="RAK4631 RTNode-2400"'

firmware-rak4631-noalloc: sketch-link
	cd $(SKETCH_LINK) && arduino-cli compile --log --fqbn rakwireless:nrf52:WisCoreRAK4631Board $(USB_ID_RAK) --output-dir $(CURDIR)/build/rak4631-noalloc \
	  --library lib/microReticulum \
	  --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x51 -DHAS_RNS -DRNS_USE_FS -DRNS_PERSIST_PATHS -fexceptions" \
	  --build-property "compiler.libraries.ldflags=-lstdc++ -lsupc++" \
	  .

# Serial-DFU flash for the RAK4631, with the same guards as flash-techo: found
# by its OWN identity (bootloader "WisBlock_RAK4631" or the renamed app),
# refused outright if that ever resolves to the medic's radio, refused when
# more than one candidate matches.
flash-rak4631:
	@z="$(CURDIR)/build/rak4631-noalloc/RNode_Firmware.ino.zip"; \
	test -f "$$z" || { echo "No image: $$z (make firmware-rak4631-noalloc)"; exit 1; }; \
	links=$$(ls /dev/serial/by-id/*RAK4631* 2>/dev/null); \
	n=$$(echo "$$links" | grep -c . || true); \
	test "$$n" = "1" || { echo "REFUSING: need exactly ONE RAK4631 on the bus (saw $$n)"; exit 1; }; \
	port=$$(readlink -f $$links); \
	for e in /dev/serial/by-id/*Espressif*; do \
	  [ -e "$$e" ] || continue; \
	  if [ "$$(readlink -f $$e)" = "$$port" ]; then \
	    echo "REFUSING: $$port is the medic's own radio"; exit 1; fi; \
	done; \
	echo "target : $$port"; echo "image  : $$(stat -c%s $$z) bytes"; \
	adafruit-nrfutil --verbose dfu serial -pkg "$$z" -p "$$port" -b 115200 --singlebank

# BISECT TARGET, one variable changed: RNS_USE_ALLOCATOR is dropped.
#
# That flag is what makes OS.cpp override GLOBAL operator new (OS.cpp:48), so
# with it every allocation in the program -- the Arduino core's included -- goes
# through TLSF, and the pool is built lazily inside the first new() call. The
# constructor table says that first call lands during STATIC INITIALISATION:
# four microReticulum containers construct at positions 5-8 of 19, while Serial
# is not constructed until 17. So anything that goes wrong in there happens
# before there is any way to report it -- which is exactly the observed failure:
# flashes clean, resets, never enumerates USB, no LED, no log.
#
# Without the flag, operator new is the ordinary one and TLSF is inert (its only
# use is inside that override). If THIS boots, the fault is in that path and not
# in RNS itself. Keep HAS_RNS so the comparison is otherwise like-for-like.
firmware-techo-noalloc: sketch-link
	cd $(SKETCH_LINK) && arduino-cli compile --log --fqbn adafruit:nrf52:pca10056 $(USB_ID) --output-dir $(CURDIR)/build/techo-noalloc \
	  --library lib/microReticulum \
	  --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x44 -DHAS_RNS -DRNS_USE_FS -DRNS_PERSIST_PATHS -fexceptions" \
	  --build-property "compiler.libraries.ldflags=-lstdc++ -lsupc++" \
	  .

flash-techo:
	@z="$(CURDIR)/build/techo-$(VARIANT)/RNode_Firmware.ino.zip"; 	test -f "$$z" || { echo "No image: $$z (make firmware-techo$(if $(filter bringup,$(VARIANT)),-bringup,))"; exit 1; }; 	link=$$(ls /dev/serial/by-id/*LilyGo_T-Echo* 2>/dev/null | head -1); 	test -n "$$link" || { echo "REFUSING: no T-Echo on the bus (double-tap RESET for DFU)"; exit 1; }; 	port=$$(readlink -f "$$link"); 	for e in /dev/serial/by-id/*Espressif*; do 	  [ -e "$$e" ] || continue; 	  if [ "$$(readlink -f $$e)" = "$$port" ]; then 	    echo "REFUSING: $$port is the medic's own radio"; exit 1; fi; 	done; 	echo "variant: $(VARIANT)"; echo "target : $$port"; 	echo "image  : $$(stat -c%s $$z) bytes"; 	adafruit-nrfutil --verbose dfu serial -pkg "$$z" -p "$$port" -b 115200 --singlebank

upload-techo:
	@test "$(PORT)" != "UNSET" || { echo "Refusing: pass PORT=/dev/serial/by-id/... explicitly"; exit 1; }
	arduino-cli upload -p $(PORT) --fqbn adafruit:nrf52:pca10056
	@sleep 6
	rnodeconf $(PORT) --firmware-hash $$(./partition_hashes from_device $(PORT))

upload-xiao_s3:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:XIAO_ESP32S3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

release: release-all

release-all: console-site spiffs-image release-tbeam release-tbeam_sx1262 release-lora32_v10 release-lora32_v20 release-lora32_v21 release-lora32_v10_extled release-lora32_v20_extled release-lora32_v21_extled release-lora32_v21_tcxo release-featheresp32 release-genericesp32 release-heltec32_v2 release-heltec32_v3 release-heltec32_v4 release-heltec32_v2_extled release-heltec_t114 release-techo release-rnode_ng_20 release-rnode_ng_21 release-t3s3 release-t3s3_sx127x release-t3s3_sx1280_pa release-tdeck release-tbeam_supreme release-rak4631 release-xiao_s3 release-hashes

# Build all PlatformIO environments and package them into a single release archive.
# The archive (rtnode_firmware.zip) is the primary distribution artefact consumed
# by flash.py.  Individual binaries are stored flat inside the zip.
release-pio:
	pio run -e rtnode_heltec_v4 -e rtnode_heltec_v3
	python3 flash.py --board v4 --merge-only --offline
	python3 flash.py --board v3 --merge-only --offline
	python3 -c "\
import zipfile, os, sys; \
variants = [ \
    ('.pio/build/rtnode_heltec_v4', 'rtnode_heltec_v4.bin'), \
    ('.pio/build/rtnode_heltec_v4', 'rtnode_heltec_v4_merged.bin'), \
    ('.pio/build/rtnode_heltec_v3', 'rtnode_heltec_v3.bin'), \
    ('.pio/build/rtnode_heltec_v3', 'rtnode_heltec_v3_merged.bin'), \
]; \
missing = [(d,n) for d,n in variants if not os.path.isfile(os.path.join(d,n))]; \
[sys.exit(f'Missing: {os.path.join(d,n)}') for d,n in missing]; \
zf = zipfile.ZipFile('rtnode_firmware.zip','w',zipfile.ZIP_DEFLATED); \
[zf.write(os.path.join(d,n), n) for d,n in variants]; \
zf.close(); \
print('Created rtnode_firmware.zip with', len(variants), 'variants'); \
"

release-hashes:
	python ./release_hashes.py > ./Release/release.json

release-rnode:
	arduino-cli compile --fqbn unsignedio:avr:rnode -e
	cp build/unsignedio.avr.rnode/RNode_Firmware.ino.hex Release/rnode_firmware.hex
	rm -r build

release-tbeam: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam.boot_app0
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bin build/rnode_firmware_tbeam.bin
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam.bootloader
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam.boot_app0 build/rnode_firmware_tbeam.bin build/rnode_firmware_tbeam.bootloader build/rnode_firmware_tbeam.partitions
	rm -r build

release-tbeam_sx1262: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\" \"-DMODEM=0x03\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam_sx1262.boot_app0
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bin build/rnode_firmware_tbeam_sx1262.bin
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam_sx1262.bootloader
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam_sx1262.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam_sx1262.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam_sx1262.boot_app0 build/rnode_firmware_tbeam_sx1262.bin build/rnode_firmware_tbeam_sx1262.bootloader build/rnode_firmware_tbeam_sx1262.partitions
	rm -r build

release-lora32_v10: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v10.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v10.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v10.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v10.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v10.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v10.boot_app0 build/rnode_firmware_lora32v10.bin build/rnode_firmware_lora32v10.bootloader build/rnode_firmware_lora32v10.partitions
	rm -r build

release-lora32_v20: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v20.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v20.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v20.boot_app0 build/rnode_firmware_lora32v20.bin build/rnode_firmware_lora32v20.bootloader build/rnode_firmware_lora32v20.partitions
	rm -r build

release-lora32_v21: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21.boot_app0 build/rnode_firmware_lora32v21.bin build/rnode_firmware_lora32v21.bootloader build/rnode_firmware_lora32v21.partitions
	rm -r build

release-lora32_v10_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v10.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v10.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v10.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v10.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v10.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v10.boot_app0 build/rnode_firmware_lora32v10.bin build/rnode_firmware_lora32v10.bootloader build/rnode_firmware_lora32v10.partitions
	rm -r build

release-lora32_v20_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v20.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v20_extled.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v20.boot_app0 build/rnode_firmware_lora32v20.bin build/rnode_firmware_lora32v20.bootloader build/rnode_firmware_lora32v20.partitions
	rm -r build

release-lora32_v21_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21_extled.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21.boot_app0 build/rnode_firmware_lora32v21.bin build/rnode_firmware_lora32v21.bootloader build/rnode_firmware_lora32v21.partitions
	rm -r build

release-lora32_v21_tcxo: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DENABLE_TCXO=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21_tcxo.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21_tcxo.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21_tcxo.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21_tcxo.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21_tcxo.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21_tcxo.boot_app0 build/rnode_firmware_lora32v21_tcxo.bin build/rnode_firmware_lora32v21_tcxo.bootloader build/rnode_firmware_lora32v21_tcxo.partitions
	rm -r build

release-heltec32_v2: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v2.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v2.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v2.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v2.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v2.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v2.boot_app0 build/rnode_firmware_heltec32v2.bin build/rnode_firmware_heltec32v2.bootloader build/rnode_firmware_heltec32v2.partitions
	rm -r build

release-heltec32_v3: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3A\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v3.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v3.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v3.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v3.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v3.boot_app0 build/rnode_firmware_heltec32v3.bin build/rnode_firmware_heltec32v3.bootloader build/rnode_firmware_heltec32v3.partitions
	rm -r build

release-heltec32_v4: check_bt_buffers
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3F\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v4pa.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v4pa.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v4pa.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v4pa.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v4pa.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v4pa.boot_app0 build/rnode_firmware_heltec32v4pa.bin build/rnode_firmware_heltec32v4pa.bootloader build/rnode_firmware_heltec32v4pa.partitions
	rm -r build

release-heltec32_v2_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v2.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v2.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v2.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v2.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v2.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v2.boot_app0 build/rnode_firmware_heltec32v2.bin build/rnode_firmware_heltec32v2.bootloader build/rnode_firmware_heltec32v2.partitions
	rm -r build

release-rnode_ng_20: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x40\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_ng20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_ng20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_ng20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_ng20.partitions
	zip --junk-paths ./Release/rnode_firmware_ng20.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_ng20.boot_app0 build/rnode_firmware_ng20.bin build/rnode_firmware_ng20.bootloader build/rnode_firmware_ng20.partitions
	rm -r build

release-rnode_ng_21: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x41\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_ng21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_ng21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_ng21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_ng21.partitions
	zip --junk-paths ./Release/rnode_firmware_ng21.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_ng21.boot_app0 build/rnode_firmware_ng21.bin build/rnode_firmware_ng21.bootloader build/rnode_firmware_ng21.partitions
	rm -r build

release-t3s3:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x03\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3.boot_app0 build/rnode_firmware_t3s3.bin build/rnode_firmware_t3s3.bootloader build/rnode_firmware_t3s3.partitions
	rm -r build

release-t3s3_sx1280_pa:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x04\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3_sx1280_pa.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3_sx1280_pa.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3_sx1280_pa.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3_sx1280_pa.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3_sx1280_pa.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3_sx1280_pa.boot_app0 build/rnode_firmware_t3s3_sx1280_pa.bin build/rnode_firmware_t3s3_sx1280_pa.bootloader build/rnode_firmware_t3s3_sx1280_pa.partitions
	rm -r build

release-t3s3_sx127x:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x01\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3_sx127x.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3_sx127x.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3_sx127x.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3_sx127x.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3_sx127x.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3_sx127x.boot_app0 build/rnode_firmware_t3s3_sx127x.bin build/rnode_firmware_t3s3_sx127x.bootloader build/rnode_firmware_t3s3_sx127x.partitions
	rm -r build

release-tdeck:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3B\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tdeck.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_tdeck.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tdeck.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tdeck.partitions
	zip --junk-paths ./Release/rnode_firmware_tdeck.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tdeck.boot_app0 build/rnode_firmware_tdeck.bin build/rnode_firmware_tdeck.bootloader build/rnode_firmware_tdeck.partitions
	rm -r build

release-tbeam_supreme:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3D\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam_supreme.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_tbeam_supreme.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam_supreme.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam_supreme.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam_supreme.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam_supreme.boot_app0 build/rnode_firmware_tbeam_supreme.bin build/rnode_firmware_tbeam_supreme.bootloader build/rnode_firmware_tbeam_supreme.partitions
	rm -r build

release-featheresp32: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:featheresp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x34\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_featheresp32.boot_app0
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.bin build/rnode_firmware_featheresp32.bin
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_featheresp32.bootloader
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_featheresp32.partitions
	zip --junk-paths ./Release/rnode_firmware_featheresp32.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_featheresp32.boot_app0 build/rnode_firmware_featheresp32.bin build/rnode_firmware_featheresp32.bootloader build/rnode_firmware_featheresp32.partitions
	rm -r build

release-genericesp32: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:esp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x35\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_esp32_generic.boot_app0
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.bin build/rnode_firmware_esp32_generic.bin
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_esp32_generic.bootloader
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_esp32_generic.partitions
	zip --junk-paths ./Release/rnode_firmware_esp32_generic.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_esp32_generic.boot_app0 build/rnode_firmware_esp32_generic.bin build/rnode_firmware_esp32_generic.bootloader build/rnode_firmware_esp32_generic.partitions
	rm -r build

release-mega2560:
	arduino-cli compile --fqbn arduino:avr:mega -e --build-property "compiler.cpp.extra_flags=\"-DMODEM=0x01\""
	cp build/arduino.avr.mega/RNode_Firmware.ino.hex Release/rnode_firmware_m2560.hex
	rm -r build

release-rak4631:
	arduino-cli compile --fqbn rakwireless:nrf52:WisCoreRAK4631Board -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x51\""
	cp build/rakwireless.nrf52.WisCoreRAK4631Board/RNode_Firmware.ino.hex build/rnode_firmware_rak4631.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_rak4631.hex Release/rnode_firmware_rak4631.zip

release-heltec_t114:
	arduino-cli compile --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3C\""
	cp build/Heltec_nRF52.Heltec_nRF52.HT-n5262/RNode_Firmware.ino.hex build/rnode_firmware_heltec_t114.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_heltec_t114.hex Release/rnode_firmware_heltec_t114.zip

release-techo:
	arduino-cli compile --log --fqbn adafruit:nrf52:pca10056 $(USB_ID) -e --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x44\""
	cp build/adafruit.nrf52.pca10056/RNode_Firmware.ino.hex build/rnode_firmware_techo.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_techo.hex Release/rnode_firmware_techo.zip

release-xiao_s3:
	arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3E\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_xiao_esp32s3.boot_app0
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bin build/rnode_firmware_xiao_esp32s3.bin
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_xiao_esp32s3.bootloader
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_xiao_esp32s3.partitions
	zip --junk-paths ./Release/rnode_firmware_xiao_esp32s3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_xiao_esp32s3.boot_app0 build/rnode_firmware_xiao_esp32s3.bin build/rnode_firmware_xiao_esp32s3.bootloader build/rnode_firmware_xiao_esp32s3.partitions
	rm -r build