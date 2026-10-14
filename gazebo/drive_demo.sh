#!/bin/bash
# Robust drive demo: republishes velocity repeatedly through each phase instead
# of publishing once, so it doesn't depend on the DiffDrive plugin holding a
# single command forever. Only needs THIS terminal -- the server (gz sim -s -r ...)
# and GUI (gz sim -g) must already be running in their own windows first.

TOPIC="/cmd_vel"
INTERVAL=0.25

publish_n() {
  local n=$1
  local msg=$2
  for ((i=0; i<n; i++)); do
    gz topic -t "$TOPIC" -m gz.msgs.Twist -p "$msg" >/dev/null 2>&1
    sleep $INTERVAL
  done
}

echo "== Forward =="
publish_n 12 "linear: {x: 0.3}"

echo "== Turn in place =="
publish_n 10 "linear: {x: 0.0}, angular: {z: 0.8}"

echo "== Forward on new heading =="
publish_n 12 "linear: {x: 0.3}, angular: {z: 0.0}"

echo "== Stop =="
gz topic -t "$TOPIC" -m gz.msgs.Twist -p "linear: {x: 0.0}, angular: {z: 0.0}"

echo "Done."
