#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "AfriSafe-Run";
const char* password = "faster1234";

// API endpoints
const char* baseUrl = "*";
const char* statusEndpoint = "/meter/status/";
const char* billEndpoint = "/meter/bill";
const char* walletEndpoint = "/meter/wallet-balance/";

// Meter configuration
const String meterNumber = "MTR-DQBTSWJT"; // Change to your meter number
const float shutoffThreshold = 0.00; // Shutoff if balance below 0 UGX
const float calibrationFactor = 4.5; // From uploaded code

// Pin configuration
#define RELAY_PIN 16 // Valve (relay) on GPIO16
#define SENSOR_PIN 27 // Flow sensor on GPIO27
#define LED_BUILTIN 2 // Built-in LED

// Flow sensor variables
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned int flowMilliLitres;
unsigned long totalMilliLitres;
long currentMillis = 0;
long previousMillis = 0;
const int interval = 1000; // 1-second interval for flow measurement

// Meter variables
unsigned long lastCheckTime = 0;
unsigned long lastSendTime = 0;
bool meterStatus = true; // Assume meter is on by default
boolean ledState = LOW;

// Interrupt service routine for flow sensor
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Start with valve closed (active-low relay)

  // Initialize flow sensor variables
  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;

  // Attach interrupt for flow sensor
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseCounter, FALLING);

  connectToWiFi();
  checkInitialStatus();
}

void loop() {
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  unsigned long currentTime = millis();

  // Check meter status and wallet balance every 5 minutes
  if (currentTime - lastCheckTime >= 300000) {
    lastCheckTime = currentTime;
    checkMeterStatus();
    checkWalletBalance();
  }

  // Measure flow every 1 second
  currentMillis = millis();
  if (currentMillis - previousMillis >= interval && meterStatus) {
    pulse1Sec = pulseCount;
    pulseCount = 0;

    // Calculate flow rate (litres/minute)
    flowRate = ((1000.0 / (currentMillis - previousMillis)) * pulse1Sec) / calibrationFactor;
    previousMillis = currentMillis;

    // Calculate millilitres in this interval
    flowMilliLitres = (flowRate / 60) * 1000;
    totalMilliLitres += flowMilliLitres;

    // Control valve based on meter status
    digitalWrite(RELAY_PIN, meterStatus ? LOW : HIGH); // LOW = open, HIGH = closed

    // Toggle LED to indicate flow
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);

    // Print flow data
    Serial.print("Flow rate: ");
    Serial.print(int(flowRate));
    Serial.print("L/min\t");
    Serial.print("Output Liquid Quantity: ");
    Serial.print(totalMilliLitres);
    Serial.print("mL / ");
    Serial.print(totalMilliLitres / 1000);
    Serial.println("L");

    // Send data to server every 1 minute or when reaching 1000ml
    if (totalMilliLitres >= 1000 || (currentTime - lastSendTime >= 60000 && totalMilliLitres > 0)) {
      sendWaterUsage(totalMilliLitres);
      totalMilliLitres = 0;
      lastSendTime = currentTime;
    }
  }
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void checkInitialStatus() {
  checkMeterStatus();
  checkWalletBalance();
}

void checkMeterStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(baseUrl) + String(statusEndpoint) + meterNumber;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      String status = doc["data"]["status"];
      meterStatus = (status == "on");
      
      Serial.print("Meter status: ");
      Serial.println(status);
      
      // Control valve
      digitalWrite(RELAY_PIN, meterStatus ? LOW : HIGH);
    } else {
      Serial.print("Error checking status: ");
      Serial.println(httpCode);
    }
    
    http.end();
  }
}

void checkWalletBalance() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(baseUrl) + String(walletEndpoint) + meterNumber;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      float balance = doc["data"]["available_balance"];
      bool isLowBalance = doc["data"]["is_low_balance"];
      
      Serial.print("Wallet balance: ");
      Serial.print(balance);
      Serial.println(" UGX");
      
      if (isLowBalance) {
        Serial.println("LOW BALANCE WARNING!");
      }
      
      // Shut off if balance below threshold
      if (balance < shutoffThreshold) {
        meterStatus = false;
        digitalWrite(RELAY_PIN, HIGH); // Close valve
        Serial.println("Balance too low - shutting off meter");
      }
    } else {
      Serial.print("Error checking balance: ");
      Serial.println(httpCode);
    }
    
    http.end();
  }
}

void sendWaterUsage(float milliliters) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(baseUrl) + String(billEndpoint));
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    DynamicJsonDocument doc(256);
    doc["meter_number"] = meterNumber;
    doc["water_reading_ml"] = milliliters;
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Water usage sent successfully");
      Serial.println(response);
      
      // Update meter status from response
      DynamicJsonDocument resDoc(1024);
      deserializeJson(resDoc, response);
      if (resDoc.containsKey("data") && resDoc["data"].containsKey("meter_status")) {
        String status = resDoc["data"]["meter_status"];
        meterStatus = (status == "on");
        digitalWrite(RELAY_PIN, meterStatus ? LOW : HIGH);
      }
    } else if (httpCode == 402) {
      Serial.println("Insufficient balance - meter should be turned off");
      meterStatus = false;
      digitalWrite(RELAY_PIN, HIGH);
    } else {
      Serial.print("Error sending water usage: ");
      Serial.println(httpCode);
    }
    
    http.end();
  }
}
