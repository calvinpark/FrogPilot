#!/usr/bin/bash

mkdir /data/openpilot/tsk || true

python3 /data/openpilot/tsk/tsk-ext.py &> /data/openpilot/tsk/ext.txt

/data/openpilot/system/ui/tsk-text /data/openpilot/tsk/ext.txt
