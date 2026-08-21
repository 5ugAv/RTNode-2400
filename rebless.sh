#!/usr/bin/env bash
# rebless.sh — heal the firmware-hash blessing after a RAW DFU flash.
#
# A BIRTHED nRF RTNode stores the blessed firmware hash in EEPROM. A raw
# `make flash-*` writes a new image but leaves the old blessing, so the boot
# check quietly refuses TNC mode and parks the radio at 0.000 MHz while BLE,
# KISS and rnodeconf all answer politely (T114, 2026-08-22 — four hours of
# wrong theories before the frequency-zero signature gave it away). This
# re-reads the device-computed hash, writes it back as the expectation, and
# COLD-cycles the USB port — the hash write triggers its own reset, but on the
# T114 only a cold boot re-ran the check.
#
# Usage: rebless.sh <by-id-glob> [more globs...]
# Exit 0: blessed (or already matching). Non-zero: needs a human.
set -u
export PATH="$HOME/.local/bin:$PATH"

# -- find exactly one app port from the globs (waiting out re-enumeration) ----
port=""
for lap in $(seq 1 12); do
  links=$(ls $@ 2>/dev/null | sort -u)
  n=$(echo "$links" | grep -c . || true)
  if [ "$n" = "1" ]; then
    pid=$(udevadm info -q property -n "$(readlink -f $links)" 2>/dev/null \
          | grep "^ID_MODEL_ID=" | cut -d= -f2)
    # the APP, not the bootloader — bootloader PIDs are the flash-gate list
    case "$pid" in 0029|002a|0071) ;; *) port=$(readlink -f "$links"); break;; esac
  fi
  sleep 5
done
[ -n "$port" ] || { echo "rebless: no single app port from: $*"; exit 1; }

# -- read target vs actual, with the nRF settle-retry (dying ttys linger) -----
target=""; actual=""
for lap in $(seq 1 6); do
  out=$(timeout 45 rnodeconf "$port" -K -L 2>&1)
  target=$(echo "$out" | grep "target firmware hash" | grep -o "[0-9a-f]\{64\}")
  actual=$(echo "$out" | grep "actual firmware hash" | grep -o "[0-9a-f]\{64\}")
  [ -n "$target" ] && [ -n "$actual" ] && break
  sleep 8
done
[ -n "$actual" ] || { echo "rebless: hash state unreadable on $port"; exit 1; }
if [ "$target" = "$actual" ]; then
  echo "rebless: blessing already matches ($actual) — nothing to do."
  exit 0
fi
echo "rebless: stored $target"
echo "rebless: actual $actual — re-blessing"
seth=$(timeout 45 rnodeconf "$port" --firmware-hash "$actual" 2>&1)
echo "$seth" | grep -q "Firmware hash set" || { echo "rebless: hash write FAILED"; echo "$seth" | tail -3; exit 1; }

# -- cold cycle so the boot check re-runs -------------------------------------
sleep 8
devpath=$(udevadm info -q path -n "$port" 2>/dev/null)
buspp=$(echo "$devpath" | grep -o "usb[0-9]*/[0-9]*-[0-9]*/" | head -1)
bus=$(echo "$buspp" | sed "s|usb\([0-9]*\)/.*|\1|")
pnum=$(echo "$buspp" | sed "s|.*/[0-9]*-\([0-9]*\)/|\1|")
if [ -n "$bus" ] && [ -n "$pnum" ] && sudo -n /usr/sbin/uhubctl -l "$bus" -p "$pnum" -a cycle >/dev/null 2>&1; then
  echo "rebless: cold-cycled bus $bus port $pnum"
else
  echo "rebless: could NOT cold-cycle automatically — UNPLUG AND REPLUG the board"
  echo "rebless: (the blessing is written; only the cold boot is missing)"
  exit 0
fi
for lap in $(seq 1 15); do
  ls $@ >/dev/null 2>&1 && { echo "rebless: board back; radio should be up — verify with a health poll"; exit 0; }
  sleep 2
done
echo "rebless: board did not re-enumerate after the cycle — check it"
exit 1
