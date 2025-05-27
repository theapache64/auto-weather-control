#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

const String serverNameFirstHit = "http://api.weatherapi.com/v1/current.json?key=" + String(WEATHER_API_KEY) + "&q=" + String(WEATHER_API_LOCATION) + "&aqi=no";
const String AUTH_TOKEN = String(AUTHORIZATION_TOKEN);
const int DEVICE_SWITCH_PIN = 0;
const int WIFI_LED = 2;
const int MIN_TEMPERATURE = 20;
const int MAX_TEMPERATURE = 25;
const bool isWorkHoursEnabled = true;
const int workHourStart = 6;  // 6 AM
const int workHourEnd = 18;   // 6 PM

// Function declarations
void setupWiFi();
void updateServerIP();
void updateDeviceStatus();
void printWeatherData(String jsonData);
String httpGETRequest(const char *serverName);
bool isWithinWorkHours(int currentHour, int startHour, int endHour);

String serverIP;
void setup()
{
  Serial.begin(115200);
  Serial.println("🌡️ Auto Weather Control - v3.0.0");

  // Initialize device switch pin to HIGH (inverted logic)
  pinMode(DEVICE_SWITCH_PIN, OUTPUT);
  digitalWrite(DEVICE_SWITCH_PIN, HIGH);

  // Initialize WiFi LED
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, LOW);

  // Connect to WiFi
  setupWiFi();

  // Get server IP
  updateServerIP();
}

void loop()
{
  delay(1000); // 1 second delay

  static unsigned long lastTime = 0;
  unsigned long timerDelay = 5000; // 5 seconds

  if ((millis() - lastTime) > timerDelay)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      digitalWrite(WIFI_LED, HIGH);
      updateDeviceStatus();
    }
    else
    {
      digitalWrite(WIFI_LED, LOW);
      Serial.println("WiFi Disconnected");
    }
    lastTime = millis();
  }
}

// Helper function to determine if current time is within work hours
bool isWithinWorkHours(int currentHour, int startHour, int endHour) {
    if (startHour <= endHour) {
        // Normal case: e.g., 9 to 17 (9 AM to 5 PM)
        return currentHour >= startHour && currentHour < endHour;
    } else {
        // Overnight case: e.g., 22 to 6 (10 PM to 6 AM)
        return currentHour >= startHour || currentHour < endHour;
    }
}

void setupWiFi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("🌐 Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("✅ Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
}

void updateServerIP()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    String jsonData = httpGETRequest(serverNameFirstHit.c_str());
    if (jsonData != "")
    {
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, jsonData);

      if (!error)
      {
        float temp = doc["current"]["temp_c"];
        Serial.println("✅ Weather API working. Current temperature: " + String(temp) + "°C");
      }
    }
  }
}

void updateDeviceStatus()
{
  String jsonData = httpGETRequest(serverNameFirstHit.c_str());
  if (jsonData != "")
  {
    printWeatherData(jsonData);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, jsonData);

    if (!error)
    {
      // Get current temperature
      float temp = doc["current"]["temp_c"];
      Serial.println("🌡️ Current temp: " + String(temp) + "°C");

      // Get current hour
      time_t now = time(nullptr);
      struct tm *timeinfo = localtime(&now);
      int currentHour = timeinfo->tm_hour;

      // Check if current time is outside work hours
      bool isOutsideWorkHours = isWorkHoursEnabled &&
                              !isWithinWorkHours(currentHour, workHourStart, workHourEnd);

      if (isWorkHoursEnabled) {
          Serial.println("📅 Work hours: " + String(workHourStart) + ":00 to " + String(workHourEnd) + ":00");
          Serial.println(isOutsideWorkHours ? "🌙 Currently outside work hours" : "🌞 Currently within work hours");
      }

      if (temp < MIN_TEMPERATURE || temp > MAX_TEMPERATURE || isOutsideWorkHours)
      {
        // Temperature is out of range or outside work hours, turn off the device
        digitalWrite(DEVICE_SWITCH_PIN, HIGH);
        Serial.println("❌ Turned OFF the device");
      }
      else
      {
        // Temperature is within range and within work hours, turn on the device
        digitalWrite(DEVICE_SWITCH_PIN, LOW);
        Serial.println("✅ Turned ON the device");
      }
    }
  }
}

void printWeatherData(String jsonData)
{
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonData);

  if (!error)
  {
    Serial.print("Country: ");
    Serial.println(doc["location"]["country"].as<String>());

    Serial.print("City: ");
    Serial.println(doc["location"]["name"].as<String>());

    Serial.print("Temperature: ");
    Serial.print(doc["current"]["temp_c"].as<float>(), 1);
    Serial.println("°C");

    Serial.print("Feels like: ");
    Serial.print(doc["current"]["feelslike_c"].as<float>(), 1);
    Serial.println("°C");

    Serial.print("Condition: ");
    Serial.println(doc["current"]["condition"]["text"].as<String>());
  }
}

String httpGETRequest(const char *serverName)
{
  WiFiClient client;
  HTTPClient http;

  // Your IP address with path or Domain name with URL path
  http.begin(client, serverName);

  // Send HTTP POST request
  int httpResponseCode = http.GET();

  String payload = "";

  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}