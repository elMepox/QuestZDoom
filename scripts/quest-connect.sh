#!/usr/bin/env bash
# Connects adb to the Quest over Wi-Fi.
#   usage: scripts/quest-connect.sh [ip[:port]]      (default: $QUEST or 192.168.1.60:5555)
# After a headset reboot adb's TCP mode is gone: plug the USB cable in once and run this script again,
# it finds the headset's Wi-Fi address over USB, switches adb to TCP and connects.
set -uo pipefail
addr="${1:-${QUEST:-192.168.1.60:5555}}"
ip="${addr%%:*}"; port="${addr##*:}"; [ "$port" = "$addr" ] && port=5555

if adb connect "$ip:$port" 2>&1 | tee /dev/stderr | grep -q "connected to"; then
  adb devices -l; exit 0
fi

usb="$(adb devices | awk '$2=="device" && $1 !~ /:/ {print $1; exit}')"
if [ -z "$usb" ]; then
  echo "Headset not reachable at $ip:$port and no USB device found." >&2
  echo "Wake the headset, check Settings > Developer > Wireless debugging is on, or plug in USB and rerun." >&2
  exit 1
fi
newip="$(adb -s "$usb" shell ip -f inet addr show wlan0 | awk '/inet /{sub(/\/.*/,"",$2); print $2; exit}')"
adb -s "$usb" tcpip "$port" >/dev/null
sleep 2
adb connect "${newip:-$ip}:$port"
adb devices -l
[ -n "$newip" ] && [ "$newip" != "$ip" ] && echo "Headset IP is now $newip: update the questAddress default in .vscode/tasks.json (or type it when asked)."
exit 0
