#include <TimerOne.h>  // Hardware timer library

// Pins
const int RELAY_PIN = 7;       // Relay control (LOW = active for low-level trigger)
const int FLOW_SENSOR_PIN = 2; // YF-S201 sensor (interrupt-capable)

// Settings
const int VALVE_INTERVAL_SEC = 5;  // Toggle valve every 5 seconds
const int DEBOUNCE_MS = 10;        // Pulse cooldown (adjust if needed)
const int CALIBRATION_FACTOR = 450; // YF-S201: ~450 pulses/liter

// Variables
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
bool valveState = false;  // Start with valve CLOSED (safety default)

void setup() {
  Serial.begin(115200);
  
  // Relay control
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Initialize OFF (for low-level trigger)

  // Flow sensor (with hardware interrupt + internal pull-up)
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);

  // Timer for valve control
  Timer1.initialize(VALVE_INTERVAL_SEC * 1000000); // microseconds
  Timer1.attachInterrupt(toggleValve);

  Serial.println(F("=== SYSTEM READY ==="));
  Serial.println(F("Time(s)\tFlow (L/min)\tValve"));
  Serial.println(F("-----------------------------"));
}

void loop() {
  static unsigned long lastPrintTime = 0;
  
  // Print diagnostics every second
  if (millis() - lastPrintTime >= 1000) {
    noInterrupts();
    float flowRate = (pulseCount / float(CALIBRATION_FACTOR)) * 60; // L/min
    pulseCount = 0;
    interrupts();

    Serial.print(millis() / 1000);  // Time in seconds
    Serial.print("\t");
    Serial.print(flowRate, 2);      // Flow rate
    Serial.print("\t\t");
    Serial.println(valveState ? "OPEN" : "CLOSED"); // Valve state

    lastPrintTime = millis();
  }
}

// Timer ISR: Toggles valve state
void toggleValve() {
  valveState = !valveState;
  digitalWrite(RELAY_PIN, valveState ? HIGH : LOW); // Adjust for your relay type
  digitalWrite(LED_BUILTIN, valveState); // Mirror state on built-in LED
}

// Debounced pulse counter for YF-S201
void pulseCounter() {
  if (millis() - lastPulseTime >= DEBOUNCE_MS) {
    pulseCount++;
    lastPulseTime = millis();
  }
}
