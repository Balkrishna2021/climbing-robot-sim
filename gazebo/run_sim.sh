#!/bin/bash
# Launch the climbing robot simulation and run the coverage demo (macOS / Linux).
# Usage:  bash run_sim.sh            -> coverage sweep
#         bash run_sim.sh drive      -> simple drive test
set -u
cd "$(dirname "$0")"

# Keep Gazebo's messaging on this machine only. Without this, macOS often
# fails with "Can't assign requested address" and velocity commands never
# reach the robot.
export GZ_IP=127.0.0.1
export GZ_PARTITION=climbing_robot

pkill -f "gz sim" 2>/dev/null; sleep 1

echo "Starting Gazebo server..."
gz sim -s -r climbing_robot_world.sdf > /tmp/gz_server.log 2>&1 &
SERVER=$!

echo "Waiting for the robot's /cmd_vel topic..."
for i in $(seq 1 30); do
  gz topic -l 2>/dev/null | grep -q "/cmd_vel" && break
  sleep 1
done

echo "Starting Gazebo GUI..."
gz sim -g > /tmp/gz_gui.log 2>&1 &
GUI=$!
sleep 8

if [ "${1:-coverage}" = "drive" ]; then
  bash drive_demo.sh
else
  bash coverage_demo.sh
fi

echo
echo "Robot position from /odom (should NOT be 0,0 if it moved):"
gz topic -e -t /model/climbing_robot/odometry -n 1 2>/dev/null | grep -A3 "position" || \
gz topic -e -t /odom -n 1 2>/dev/null | grep -A3 "position"

echo
echo "Done. Close the Gazebo window or press Ctrl+C here to stop."
wait $GUI
kill $SERVER 2>/dev/null
