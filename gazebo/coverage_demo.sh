#!/bin/bash
# Autonomous surface-coverage demo: boustrophedon ("lawnmower") sweep pattern.
# Demonstrates the "path following and/or autonomous surface coverage" part
# of the Task 2 brief, using the same open-loop gz-topic publishing style as
# drive_demo.sh. Run in its own terminal, AFTER the server (gz sim -s -r)
# and GUI (gz sim -g) are already running.
#
# Pattern: forward one row -> turn ~90deg -> short step-over -> turn ~90deg
# (same rotation direction) -> forward the next row on the OPPOSITE heading
# -> repeat. This sweeps a rectangular strip of the (currently flat) test
# surface -- the same principle a tower-climbing version would use to cover
# a cylindrical shell: sweep one ring, step up (or across) one brush-width,
# sweep the next ring. Note: the real tower-surface
# version needs the crawler/track adhesion from the Fusion CAD (Task 1),
# not wheeled friction on a flat plane -- this proves the PATH-PLANNING and
# CONTROL logic, the physical climbing mechanism is a separate concern.
#
# LIMITATION: turns are OPEN-LOOP,
# timed by publish count, not closed-loop on IMU/odometry feedback -- fine
# for a demo, not for a real deployment, where drift would need correcting
# using the wheel-odometry the DiffDrive plugin already publishes on /odom.

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

FORWARD_ROW=12   # ~3s forward travel = one "row" of coverage
STEP_OVER=4      # short side-step between rows
TURN_90=10       # ~90 degree turn (same calibration as drive_demo.sh)
ROWS=3

echo "== Row 1: forward =="
publish_n $FORWARD_ROW "linear: {x: 0.3}"

for ((row=1; row<ROWS; row++)); do
  echo "== Turn to step over (row $row -> $((row+1))) =="
  publish_n $TURN_90 "linear: {x: 0.0}, angular: {z: 0.8}"

  echo "== Step-over =="
  publish_n $STEP_OVER "linear: {x: 0.2}"

  echo "== Turn onto next row (opposite heading) =="
  publish_n $TURN_90 "linear: {x: 0.0}, angular: {z: 0.8}"

  echo "== Row $((row+1)): forward =="
  publish_n $FORWARD_ROW "linear: {x: 0.3}"
done

echo "== Stop =="
gz topic -t "$TOPIC" -m gz.msgs.Twist -p "linear: {x: 0.0}, angular: {z: 0.0}"

echo "Coverage sweep complete ($ROWS rows)."
