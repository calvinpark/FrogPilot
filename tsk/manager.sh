#!/usr/bin/env bash
set -v

CANARY="/persist/alreadyRW"

cleanup() {
  if [ -f ${CANARY} ]; then
    # /persist was already RW before we started.
    # Remove the marker and don't remount as read-only.
    rm ${CANARY}
  else
    # Remount /persist as read-only
    sudo mount -o remount,ro /persist
  fi

  # Restart
  sudo reboot
}
trap cleanup EXIT

# Make it possible to write to /persist/tsk
touch ${CANARY}
if [ $? -ne 0 ]; then
  sudo mount -o remount,rw /persist
fi
mkdir -p /persist/tsk || true
chown comma /persist/tsk

# Run and exit
cd /data/openpilot
system/ui/tsk-manager
