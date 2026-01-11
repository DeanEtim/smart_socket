/*
 * Smart IoT Socket for Energy Optimization
 * Author: Dean Etim (Radiant Tech)
 * Platform: ESP32
 */

// Libraries
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include <Adafruit_ADS1X15.h>
#include <LiquidCrystal_I2C.h>

#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi Configuration
const char *ssid = "my_mifi_network";
const char *password = "12345678";

// Firebase Configuration
#define API_KEY "YOUR_FIREBASE_API_KEY"                        // Firebase API key
#define DATABASE_URL "https://your-project-id.firebaseio.com/" // database endpoint

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Hardware Pins
const uint8_t relayPin = 19;
const uint8_t buttonPin = 2;

// ADC and LCD
const uint8_t lcdAddress = 0x27;
const uint8_t adsAddress = 0x48;
const uint8_t voltageChannel = 0;
const uint8_t currentChannel = 1;

LiquidCrystal_I2C lcd(lcdAddress, 16, 2);
Adafruit_ADS1115 ads;

// Measurement Constants
const float adcLsb = 0.000125;                // ADS1115 LSB (GAIN_ONE)
const float voltageCal = 13.965 / 3.284;      // Voltage divider calibration
const float transformerRatio = 219.9 / 7.965; // Voltage transformer ratio
const float overVoltageLimit = 260.0;         // Overvoltage safety limit in volts(V)

// State Variables
bool socketState = true; // socket is ON by default
bool lastButtonState = HIGH;

double totalEnergyKwh = 0.0;

unsigned long lastMeasurementMs = 0;
const unsigned long measurementIntervalMs = 1000;

// Function Protoypes
void setupWifi();                                                                   // initialize WiFi connection
void setupFirebase();                                                               // initialize Firebase connection
void handleOvervoltage(double vrms);                                                // safety feature to turn off relay on overvoltage
void handleButtonInput();                                                           // handle button input to switch socket ON/OFF
void syncRelayFromFirebase();                                                       // sync relay state from Firebase
void pushLiveDataToFirebase(double vrms, double irms, double power, double energy); // push live data to Firebase
double measureVrms(uint8_t channel);                                                // measure voltage RMS
double measureIrms(uint8_t channel);                                                // measure current RMS
double computeApparentPower(double vrms, double irms);                              // compute apparent power in watts
void updateLcd(double vrms, double power);                                          // update LCD display with voltage and power measurements

void setup()
{
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  digitalWrite(relayPin, socketState); // socket is ON by default

  Wire.begin();
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("--Smart Socket--");

  if (!ads.begin(adsAddress))
  {
    Serial.println("ADS1115 initialization failed");
  }
  else
  {
    ads.setGain(GAIN_ONE);
    ads.setDataRate(RATE_ADS1115_860SPS);
  }

  setupWifi();
  setupFirebase();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  delay(1000);
  lcd.clear();
} // end setup()

void loop()
{
  // Handle button input and sync relay state from Firebase to turn on/off socket
  handleButtonInput();
  syncRelayFromFirebase();

  unsigned long now = millis();
  if (now - lastMeasurementMs >= measurementIntervalMs)
  {
    double vrms = measureVrms(voltageChannel);       // measure voltage
    double irms = measureIrms(currentChannel);       // measure current
    double power = computeApparentPower(vrms, irms); // compute apparent power in watts

    totalEnergyKwh += (power / 1000.0) / 3600.0; // accumulate energy in kWh

    handleOvervoltage(vrms);                                   // safety feature
    updateLcd(vrms, power);                                    // update LCD display with voltage and power measurements
    pushLiveDataToFirebase(vrms, irms, power, totalEnergyKwh); // push live data to Firebase

    Serial.printf(
        "V: %.2f V || I: %.2f A || P: %.2f W || E: %.4f kWh\n",
        vrms, irms, power, totalEnergyKwh);

    lastMeasurementMs = now;
  }
} // end loop()

void setupWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Smart Socket  ");
  lcd.setCursor(0, 1);
  lcd.print("Connecting to WiFi...");

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Smart Socket");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected!");

  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());
} // end setupWifi()

void setupFirebase()
{
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
} // end setupFirebase()

void handleOvervoltage(double vrms)
{ // safety feature: turn off relay on overvoltage
  if (vrms > overVoltageLimit)
  {
    socketState = false;
    digitalWrite(relayPin, socketState);
    Firebase.RTDB.setBool(&fbdo, "/smartSocket/relay/state", false);
    Firebase.RTDB.setString(&fbdo, "/smartSocket/alerts", "Overvoltage detected");
  }
} // end handleOvervoltage()

void handleButtonInput()
{
  bool currentState = digitalRead(buttonPin);

  if (lastButtonState == HIGH && currentState == LOW)
  {
    socketState = !socketState;
    digitalWrite(relayPin, socketState);
    Firebase.RTDB.setBool(&fbdo, "/smartSocket/relay/state", socketState);
  }

  lastButtonState = currentState;
} // end handleButtonInput()

void syncRelayFromFirebase()
{
  if (Firebase.RTDB.getBool(&fbdo, "/smartSocket/relay/state"))
  {
    bool newState = fbdo.boolData();
    socketState = newState;
    digitalWrite(relayPin, socketState);
  }
} // end syncRelayFromFirebase()

void pushLiveDataToFirebase(double vrms, double irms, double power, double energy)
{
  Firebase.RTDB.setFloat(&fbdo, "/smartSocket/live/voltage", vrms);
  Firebase.RTDB.setFloat(&fbdo, "/smartSocket/live/current", irms);
  Firebase.RTDB.setFloat(&fbdo, "/smartSocket/live/power", power);
  Firebase.RTDB.setFloat(&fbdo, "/smartSocket/live/energy_kwh", energy);
} // end pushLiveDataToFirebase()

double measureVrms(uint8_t channel)
{
  int16_t raw = ads.readADC_SingleEnded(channel);
  double vAdc = raw * adcLsb;
  double vrms = (vAdc * voltageCal + 1.4) / sqrt(2) * transformerRatio;
  return (vrms < 30.0) ? 0.0 : vrms;
} // end measureVrms()

double measureIrms(uint8_t channel)
{
  const int sampleCount = 1000;       // number of samples
  const double ctRatio = 2000.0;      // current transformer turns ratio
  const double burdenResistor = 90.0; // sampling resistor

  double sum = 0.0;
  double samples[sampleCount];

  for (int i = 0; i < sampleCount; i++)
  {
    double v = ads.readADC_SingleEnded(channel) * adcLsb;
    samples[i] = v;
    sum += v;
  }

  double mean = sum / sampleCount;
  double sqSum = 0.0;

  for (int i = 0; i < sampleCount; i++)
  {
    double centered = samples[i] - mean;
    sqSum += centered * centered;
  }

  double vrms = sqrt(sqSum / sampleCount);
  double secondaryCurrent = vrms / burdenResistor;
  return secondaryCurrent * ctRatio;
} // end measureIrms()

double computeApparentPower(double vrms, double irms)
{
  double power = vrms * irms;
  return (power < 0.6) ? 0.0 : power;
} // end computeApparentPower()

void updateLcd(double vrms, double power)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(vrms, 1);
  lcd.print("  ");

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(power, 1);
  lcd.print("W ");
} // end updateLcd()
