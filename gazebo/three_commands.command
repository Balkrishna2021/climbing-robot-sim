#!/bin/bash
# Double-click to start the climbing robot simulation and send it three commands.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cd "$(dirname "$0")"
export GZ_IP=127.0.0.1
export GZ_PARTITION=climbing_robot

if ! command -v gz >/dev/null; then
  echo "Gazebo (gz) was not found on this Mac's PATH."; read -p "Press Enter to close."; exit 1
fi

pkill -f "gz sim" 2>/dev/null; sleep 1
echo "Starting Gazebo server..."
gz sim -s -r climbing_robot_world.sdf > /tmp/gz_server.log 2>&1 &
SERVER=$!
for i in $(seq 1 30); do gz topic -l 2>/dev/null | grep -q "/cmd_vel" && break; sleep 1; done
echo "Starting Gazebo window..."
gz sim -g > /tmp/gz_gui.log 2>&1 &
GUI=$!
echo "Waiting 10 s for the window to open..."; sleep 10

send() {  # $1 = message, $2 = seconds
  local n=$(( $2 * 4 ))
  for ((i=0; i<n; i++)); do gz topic -t /cmd_vel -m gz.msgs.Twist -p "$1" >/dev/null 2>&1; sleep 0.25; done
}
pose() {
  gz topic -e -t /model/climbing_robot/odometry -n 1 2>/dev/null | awk '/position/{f=1} f&&/x:/{x=$2} f&&/y:/{print "   position: x=" x "  y=" $2; exit}'
}

echo; echo "Command 1: drive forward at 0.3 m/s for 4 s"
send "linear: {x: 0.3}" 4; pose
echo; echo "Command 2: turn left on the spot for 2 s"
send "linear: {x: 0.0}, angular: {z: 0.8}" 2; pose
echo; echo "Command 3: drive forward on the new heading for 4 s, then stop"
send "linear: {x: 0.3}" 4
gz topic -t /cmd_vel -m gz.msgs.Twist -p "linear: {x: 0.0}, angular: {z: 0.0}" >/dev/null 2>&1; pose

echo; echo "Done. If the positions above changed, the robot moved."
echo "Close the Gazebo window when finished."
wait $GUI; kill $SERVER 2>/dev/null
