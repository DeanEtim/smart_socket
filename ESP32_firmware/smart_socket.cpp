// Libraries
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "webDashBoard.h"

// WiFi SoftAP Configuration
const char *ssid = "Smart Socket";
const char *password = "deanProject";
IPAddress local_ip(192, 168, 5, 3);
IPAddress gateway(192, 168, 5, 3);
IPAddress subnet(255, 255, 255, 0);

// Server Objects
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ADS1115 and LCD Configuration
const float FSR = 4.096;  // full scale
const float ADC_LSB = 0.000125;

int8_t LCD_ADDR = 0x27;
int8_t ADS_ADDRESS = 0x48;

const int voltageChannel = 0;
const int currentChannel = 1;

const float VOLTAGE_CAL = 13.965 / 3.284;      // voltage divider factor
const float transformerRatio = 219.9 / 7.965;  // turns ratio

LiquidCrystal_I2C screen(LCD_ADDR, 16, 2);
Adafruit_ADS1115 ads;

// Variables for Measurements
double totalEnergy_kWh = 0.0;
unsigned long previousMillis = 0;

// Function Prototypes
void displayLiveReading(double Vrms, double Irms, double apparentPower);
void showWelcomeMessage(String message);                                            // Displays a scrolling text message
void notifyClients(double Vrms, double Irms, double apparentPower, double energy);  // Pushes the measured parameters to the websocket server
void webSocketEvents(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void setupWiFi();                                                       // Sets the ESP32 as Wifi SoftAP
void handleRoot();                                                      // Serves the webpage from SPIFFS
double measureVrms(uint8_t channel);                                    // Calculates the RMS voltage
double measureIrms(uint8_t channel);                                    // Calculates the RMS current
double computeApparentPower(double measuredVrms, double measuredIrms);  // Calculates Apparent Power

void setup() {
  // Intialize Serial Monitor for debugging
  Serial.begin(115200);

  // Initialize LCD
  Wire.begin();
  screen.init();
  screen.backlight();

  // Initialize the ADS1115
  if (!ads.begin(ADS_ADDRESS)) {
    Serial.println("Failed to initialize ADS1115!");
  } else {
    ads.setGain(GAIN_ONE);
    ads.setDataRate(RATE_ADS1115_860SPS);
  }

  // Display the initialization message
  showWelcomeMessage("Wattmeter Initializing...");

  // Setup ESP32 as a SoftAP
  setupWiFi();

  // Serve the webpage from SPIFFS on request
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started..");

  // Start WebSocket Server
  webSocket.begin();
  webSocket.onEvent(webSocketEvents);
  Serial.println("System Ready!");
}  // end setup

void loop() {
  webSocket.loop();
  server.handleClient();

  uint32_t currentMillis = millis();
  double Vrms = measureVrms(voltageChannel);
  double Irms = measureIrms(currentChannel);
  double apparentPower = computeApparentPower(Vrms, Irms);

  if (currentMillis - previousMillis >= 1000) {
    displayLiveReading(Vrms, Irms, apparentPower);
    totalEnergy_kWh += (apparentPower / 1000.0) / 3600.0;

    // Also push live data to WebSocket clients
    notifyClients(Vrms, Irms, apparentPower, totalEnergy_kWh);

    previousMillis = currentMillis;
  }
}  // end main loop

void setupWiFi() {
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.print("SoftAP IP Address: ");
  Serial.println(WiFi.softAPIP());
}  // end setupWifi()

void handleRoot() {
  server.send_P(200, "text/html", myWebPage);
}  // end serveWebPage()

void notifyClients(double Vrms, double Irms, double apparentPower, double energy) {
  // Create JSON message
  StaticJsonDocument<256> doc;
  doc["voltage"] = Vrms;
  doc["current"] = Irms;
  doc["power"] = apparentPower;
  doc["energy"] = energy;

  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.broadcastTXT(jsonString);
}  // end notifyClients()

void displayLiveReading(double Vrms, double Irms, double apparentPower) {
  screen.clear();
  screen.setCursor(0, 0);
  screen.print("Voltage: ");
  screen.print(Vrms, 1);
  screen.print("V");

  screen.setCursor(0, 1);
  screen.print("Load: ");
  screen.print(apparentPower, 2);
  screen.print("W");

  Serial.printf("Vrms: %.2fV | Irms: %.2fA | P: %.2fW | E: %.4fkWh\n",
                Vrms, Irms, apparentPower, totalEnergy_kWh);
}  // end ComputePowerAndDisplay()

double measureVrms(uint8_t channel) {
  int16_t raw = ads.readADC_SingleEnded(channel);
  double Vadc = (double)raw * ADC_LSB;
  double Vrms = (Vadc * VOLTAGE_CAL + 1.4) / sqrt(2) * transformerRatio;

  return (Vrms < 30.0) ? 0.0 : Vrms;  // if measured voltage is less than 30Vrms, make it 0.0Vrms
}  // end measureVrms()

double measureIrms(uint8_t channel) {
  const int n_samples = 1000;         // Number of samples to average
  const double CT_ratio = 2000.0;     // Current transformer turns ratio
  const double burden_resistor = 90.0;  // Burden resistor value

  double sum = 0.0;
  double samples[n_samples];

  for (int i = 0; i < n_samples; i++) {
    int16_t raw = ads.readADC_SingleEnded(channel);  // signed 16-bit reading
    double voltage = raw * ADC_LSB;
    samples[i] = voltage;
    sum += voltage;
  }

  // Calculate the mean voltage
  double mean = sum / n_samples;

  // Remove the offset and accumulate the squared values
  double sqSum = 0;
  for (int i = 0; i < n_samples; i++) {
    double centered = samples[i] - mean;
    sqSum += centered * centered;
  }

  // Calculate the AC voltage at the Burden resistor
  double Vrms = sqrt(sqSum / n_samples);

  // Convert to secondary current
  double Is_rms = Vrms / burden_resistor;

  // Convert to primary current
  double Ip_rms = Is_rms * CT_ratio;
  return Ip_rms;
}  // end measureIrms()

double computeApparentPower(double measuredVrms, double measuredIrms) {
  double apparentPower = measuredVrms * measuredIrms;
  return (apparentPower < 0.6) ? 0.0 : apparentPower;
}  // end computeRealPower()

void showWelcomeMessage(String message) {
  screen.setCursor(0, 0);
  screen.print(message.substring(0, 16));
  delay(500);
  for (int pos = 0; pos <= message.length(); pos++) {
    screen.clear();
    screen.print(message.substring(pos, pos + 16));
    delay(250);
  }
  screen.clear();
}  // end showWelcomeMessage()

void webSocketEvents(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("Client [%u] connected\n", num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("Client [%u] disconnected\n", num);
  } else if (type == WStype_TEXT) {
    String msg = String((char *)payload);
    Serial.printf("Received message: %s\n", msg.c_str());
  }
}  // end webSocketEvents()
