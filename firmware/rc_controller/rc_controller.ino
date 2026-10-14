/*
 * Climbing robot - robot-side RC receiver / motor controller
 * ESP32 sketch, simulated in Wokwi.
 *
 * See the repository README for the overall design and what this file
 * simulates vs proposes.
 *
 * WHAT THIS DEMONSTRATES LIVE:
 *   - Arcade-drive mixing (throttle + turn -> left/right PWM)
 *   - Mode arbitration: the SAME mixing/motor-drive path handles both a
 *     human joystick command (RC mode) and a canned command sequence that
 *     stands in for the autonomous coverage-planner's output (AUTO mode)
 *   - A hardware-priority E-stop gate applied last, unconditionally, after
 *     mixing and after mode selection -- so nothing upstream can bypass it
 *
 * WHAT THIS DOES NOT SIMULATE (proposed, not built -- said up front):
 *   - The actual wireless link (ESP-NOW). Wokwi can't simulate two
 *     wirelessly-linked boards talking to each other in one project, so the
 *     joystick here stands in for "a command packet has just arrived".
 *   - The watchdog that detects a LOST radio link. signalTimedOut() below
 *     is a stub showing exactly where that check plugs into the same
 *     safety gate as the E-stop -- see README.
 *   - A real L298N/H-bridge + DC motors. Wokwi has no OFFICIAL built-in
 *     motor-driver part -- only community custom chips exist for that
 *     (e.g. drf5n/Wokwi-Chip-L298N), and those require their compiled chip
 *     source to be bundled inside the project or they render as "Missing
 *     chip Breakout". Rather than depend on an external chip that isn't
 *     part of this project, the two motor channels are represented by four
 *     PWM-driven LEDs -- one forward + one reverse indicator per side,
 *     brightness = PWM duty cycle -- which uses only official Wokwi parts
 *     and is 100% reliable. The control logic driving them (arcade mixing,
 *     mode mux, E-stop gate) is identical either way; only the physical
 *     actuator visualisation changed.
 */

#define ENABLE_OLED 1

#if ENABLE_OLED
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

// ---------------- Pin map (Wokwi ESP32 wiring) ----------------
const int PIN_JOY_VERT  = 34; // throttle, ADC1_CH6, input-only pin
const int PIN_JOY_HORZ  = 35; // turn,     ADC1_CH7, input-only pin
const int PIN_BTN_ESTOP = 33; // press to toggle E-stop latch
const int PIN_BTN_MODE  = 26; // press to toggle RC <-> AUTONOMOUS

// Motor channels are represented as PWM-driven LED pairs (forward/reverse
// indicator per side) rather than a real H-bridge -- see header comment.
const int PIN_LED_LFWD = 27; // left motor, forward direction, brightness = |PWM|
const int PIN_LED_LREV = 14; // left motor, reverse direction, brightness = |PWM|
const int PIN_LED_RFWD = 13; // right motor, forward direction, brightness = |PWM|
const int PIN_LED_RREV = 4;  // right motor, reverse direction, brightness = |PWM|

const int PIN_LED_MODE  = 18; // lit = AUTONOMOUS, off = RC
const int PIN_LED_ESTOP = 23; // lit = E-stop engaged (or watchdog tripped)

const uint32_t PWM_FREQ = 20000; // 20 kHz -- above audible range on a real driver, avoids whine
const uint8_t  PWM_RES  = 8;     // 8-bit duty cycle, 0..255

// ---------------- Shared command struct -------------------------------
// Deliberately the SAME struct a real ESP-NOW packet would carry, so the
// motor-control code below never needs to know whether a command came
// over the air or from the autonomous planner (see README).
struct RCCommand {
  int16_t throttle; // -255..255
  int16_t turn;     // -255..255
  uint8_t mode;     // 0 = RC, 1 = AUTONOMOUS
  uint8_t estop;    // 1 = engaged
};

bool modeIsAuto     = false;
bool estopLatched   = false;
bool lastEstopBtn   = false;
bool lastModeBtn    = false;

// In the real system this is updated every time a verified ESP-NOW packet
// arrives, and signalTimedOut() returns true once ~300ms have passed with
// no fresh packet -- protecting against a robot coasting on a stale command
// if the link drops mid-climb. Not wireless here, so always "fresh".
unsigned long lastPacketMs = 0;
bool signalTimedOut() {
  return false; // real: return millis() - lastPacketMs > 300;
}

RCCommand readJoystickAsRC() {
  int rawV = analogRead(PIN_JOY_VERT); // 0..4095
  int rawH = analogRead(PIN_JOY_HORZ);
  RCCommand c;
  c.throttle = map(rawV, 0, 4095, -255, 255);
  c.turn     = map(rawH, 0, 4095, -255, 255);
  if (abs(c.throttle) < 12) c.throttle = 0; // deadzone: centred stick reads exactly 0
  if (abs(c.turn) < 12) c.turn = 0;
  c.mode  = 0;
  c.estop = 0; // estop is OR'd in by the caller, independent of source
  lastPacketMs = millis();
  return c;
}

RCCommand autonomousDemoCommand() {
  // Canned stand-in for Task 2's Gazebo coverage-planner output: forward,
  // then a gentle arc, repeating. Proves the mixing/motor path is source-
  // agnostic -- the exact point Section 7 of the design doc makes.
  unsigned long t = millis() % 6000;
  RCCommand c;
  if (t < 3000) { c.throttle = 160; c.turn = 0;  }
  else          { c.throttle = 140; c.turn = 90; }
  c.mode  = 1;
  c.estop = 0;
  return c;
}

// Drives one motor channel as a forward/reverse LED pair: brightness is the
// PWM magnitude, only one of the pair is ever lit at a time. Same signal
// shape (0..255 duty cycle) a real H-bridge's PWM enable line would carry.
void driveMotor(int fwdPin, int revPin, int speedSigned) {
  int mag = constrain(abs(speedSigned), 0, 255);
  if (speedSigned >= 0) {
    ledcWrite(fwdPin, mag);
    ledcWrite(revPin, 0);
  } else {
    ledcWrite(fwdPin, 0);
    ledcWrite(revPin, mag);
  }
}

void applyCommand(RCCommand cmd) {
  int left  = constrain(cmd.throttle + cmd.turn, -255, 255);
  int right = constrain(cmd.throttle - cmd.turn, -255, 255);

  // ---- SAFETY GATE ----
  // Always last, always unconditional. Nothing above this line -- not the
  // mixing, not the mode mux -- can put a nonzero value on the motors once
  // either check trips. This ordering is the actual design point being
  // demonstrated, not just the code that happens to run.
  bool safetyTripped = (cmd.estop == 1) || signalTimedOut();
  if (safetyTripped) {
    left = 0;
    right = 0;
  }

  driveMotor(PIN_LED_LFWD, PIN_LED_LREV, left);
  driveMotor(PIN_LED_RFWD, PIN_LED_RREV, right);

  digitalWrite(PIN_LED_MODE, cmd.mode == 1 ? HIGH : LOW);
  digitalWrite(PIN_LED_ESTOP, safetyTripped ? HIGH : LOW);

#if ENABLE_OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("MODE : "); display.println(cmd.mode == 1 ? "AUTO" : "RC");
  display.print("ESTOP: "); display.println(safetyTripped ? "ENGAGED" : "clear");
  display.print("L PWM: "); display.println(left);
  display.print("R PWM: "); display.println(right);
  display.display();
#endif

  Serial.printf("mode=%s estop=%d L=%d R=%d\n",
                cmd.mode == 1 ? "AUTO" : "RC ", safetyTripped ? 1 : 0, left, right);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_ESTOP, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE,  INPUT_PULLUP);
  pinMode(PIN_LED_MODE,  OUTPUT);
  pinMode(PIN_LED_ESTOP, OUTPUT);

  // arduino-esp32 core 3.x LEDC API: attach directly to a pin, no manual
  // channel bookkeeping (older core used ledcSetup()+ledcAttachPin()).
  ledcAttach(PIN_LED_LFWD, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LED_LREV, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LED_RFWD, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LED_RREV, PWM_FREQ, PWM_RES);

#if ENABLE_OLED
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed -- continuing on Serial output only");
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
#endif

  Serial.println("Climbing robot RC receiver -- simulation boot OK");
  Serial.println("Blue button = toggle RC/AUTO mode. Red button = toggle E-stop.");
}

void loop() {
  // Edge-triggered so a held button doesn't rapid-toggle.
  bool estopBtn = (digitalRead(PIN_BTN_ESTOP) == LOW);
  if (estopBtn && !lastEstopBtn) {
    estopLatched = !estopLatched; // real hardware: a latching E-stop switch
  }
  lastEstopBtn = estopBtn;

  bool modeBtn = (digitalRead(PIN_BTN_MODE) == LOW);
  if (modeBtn && !lastModeBtn) {
    modeIsAuto = !modeIsAuto; // real system: this flag arrives inside the RC packet
  }
  lastModeBtn = modeBtn;

  RCCommand cmd = modeIsAuto ? autonomousDemoCommand() : readJoystickAsRC();
  cmd.mode  = modeIsAuto ? 1 : 0;
  cmd.estop = estopLatched ? 1 : 0; // E-stop always wins, independent of source

  applyCommand(cmd);

  delay(50); // ~20 Hz loop, matches the assumed 50 Hz transmit rate order-of-magnitude
}
