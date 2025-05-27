#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <Keys.h>
#include <NTPClient.h>
#include <Servo.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUDP.h>

#include <NetworkClient.cpp>
#include <WiFi.cpp>
#include <map>

#include "DHT.h"

#define BUZZER_PIN D2
#define DHTPIN D1
#define SERVO_PIN D0

#define DHTTYPE DHT22  // Sensor type
DHT dht(DHTPIN, DHTTYPE);

Servo powerButtonServo;

// Boolean to enable or disable SERVO
bool servoEnabled = false;

// Global variables
WiFiConnection wifi;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 19800, 3600000);  // 1 hour
NetworkClient client;
std::map<String, String> config;

void uploadDhtData(float temperature, float humidity, float score, String note);
void pressPowerButton();
void logTelegram(String msg);
float calculateScore(float temperature, float humidity);

// Helper function to check if current hour is within work hours range
bool isWithinWorkHours(int currentHour, int startHour, int endHour) {
    if (startHour <= endHour) {
        // Normal case: e.g., 9 to 17 (9 AM to 5 PM)
        return currentHour >= startHour && currentHour < endHour;
    } else {
        // Overnight case: e.g., 22 to 6 (10 PM to 6 AM)
        return currentHour >= startHour || currentHour < endHour;
    }
}

std::map<String, String> fetchConfig() {
    std::map<String, String> data;
    HTTPClient formRequest;
    if (formRequest.begin(*client.httpClient, GOOGLE_SHEET_URL)) {
        int responseCode = formRequest.GET();
        if (responseCode > 0) {
            String payload = formRequest.getString();
            // Parse the payload and populate the data map

            int startPos = 0;
            int endPos = payload.indexOf('\n');
            bool isLastItem = false;
            while (endPos != -1) {
                Serial.println("---------------");

                String line = payload.substring(startPos, endPos);

                int commaPos = line.indexOf(',');
                if (commaPos != -1) {
                    String key = line.substring(1, commaPos - 1);
                    key.replace("\"", "");
                    String value =
                        line.substring(commaPos + 2, line.length() - 1);
                    value.replace("\"", "");

                    data[key] = value;
                }
                startPos = endPos + 1;
                endPos = payload.indexOf('\n', startPos);
                if (isLastItem) {
                    break;
                }

                if (endPos == -1) {
                    endPos = payload.length();
                    isLastItem = true;
                }
            }
        }
    }

    return data;
}

void beep() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
}

void beepTwice() {
    beep();
    delay(200);
    beep();
}

void setup() {
    Serial.begin(115200);

    if(servoEnabled) {
        Serial.println("🤖 Servo system is ready and enabled");

        // servo
        powerButtonServo.attach(SERVO_PIN);
        delay(1000);
        powerButtonServo.write(90);
        delay(200);
        powerButtonServo.write(180);  // hands up
    } else {
        Serial.println("ℹ️ Servo system is currently disabled");
    }


    // buzzer
    pinMode(BUZZER_PIN, OUTPUT);

    // wifi
    wifi.connectToWifi();

    // temperature and humidity sensor
    dht.begin();

    // config
    config = fetchConfig();

    // time
    timeClient.begin();

    beepTwice();
}

enum AcState { OFF, ON };
AcState acState = OFF;

int alreadyWarningCount = 0;
int maxAlreadyWarningCount = -1;

unsigned long acTurnOnAt = 0;
unsigned long acTurnOffAt = 0;

void loop() {
    String telegramLog = "";
    if ((WiFi.status() == WL_CONNECTED)) {
        timeClient.update();
        int currentHour = timeClient.getHours();

        config = fetchConfig();
        if (config.empty()) {
            telegramLog += "🚨 Unable to load configuration. Please check the connection.";
        } else {
            bool shouldSkip = config["should_skip"] == "TRUE";
            bool isWorkHoursEnabled = config["is_work_hours_enabled"] == "TRUE";
            int workHourStart = config["work_hour_start"].toInt();
            int workHourEnd = config["work_hour_end"].toInt();
            String mode = config["mode"];
            bool isOnOff = mode == "ON_OFF";
            bool isOutsideWorkHours = isWorkHoursEnabled &&
                                    !isWithinWorkHours(currentHour, workHourStart, workHourEnd);

            Serial.println("🕐 Current time: " + String(currentHour) + ":00");
            Serial.println(shouldSkip ? "⏭️ System is set to skip" : "✅ System is active");
            Serial.println(isWorkHoursEnabled ? "⏰ Work hours mode is active" : "🔄 24/7 mode is active");
            if (isWorkHoursEnabled) {
                Serial.println("📅 Work hours: " + String(workHourStart) + ":00 to " + String(workHourEnd) + ":00");
                Serial.println(isOutsideWorkHours ? "🌙 Currently outside work hours" : "🌞 Currently within work hours");
            }

            // Rest of the file remains the same