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
            int isOutsideWorkHours = isWorkHoursEnabled &&
                                     currentHour <= workHourStart &&
                                     currentHour >= workHourEnd;

            Serial.println("🕐 Current time: " + String(currentHour) + ":00");
            Serial.println(shouldSkip ? "⏭️ System is set to skip" : "✅ System is active");
            Serial.println(isWorkHoursEnabled ? "⏰ Work hours mode is active" : "🔄 24/7 mode is active");
            if (isWorkHoursEnabled) {
                Serial.println("📅 Work hours: " + String(workHourStart) + ":00 to " + String(workHourEnd) + ":00");
            }

            String note = "";

            if (shouldSkip) {
                Serial.println("⏸️ System is paused - skipping temperature check");
                telegramLog += "\n\n⏸️ System is currently paused. Temperature monitoring will resume when skip mode is disabled.";
            } else if (isOutsideWorkHours) {
                Serial.println("🌙 Outside work hours - system is resting");
                telegramLog += "\n\n🌙 It's " + String(currentHour) + ":00 - outside working hours. System will resume during work hours.";
            } else {
                maxAlreadyWarningCount =
                    config["max_already_warning_count"].toInt();

                boolean isForceModeEnabled = config["force_mode"] == "TRUE";
                float temperature;
                float humidity;
                if (isForceModeEnabled) {
                    temperature = dht.readTemperature(false, true);
                    humidity = dht.readHumidity();
                } else {
                    // run 2 times to fix stale data issue
                    for (int i = 0; i < 2; i++) {
                        temperature = dht.readTemperature();
                        humidity = dht.readHumidity();
                        Serial.println("🌡️ Current temperature: " + String(temperature) + "°C");
                        Serial.println("💧 Current humidity: " + String(humidity) + "%");
                        delay(5000);
                    }
                }

                if (isnan(temperature) || isnan(humidity)) {
                    Serial.println("⚠️ Unable to read sensor data");
                    telegramLog += "\n\n⚠️ Sensor reading error - Temperature:" + String(temperature) + ", Humidity:" + String(humidity) + ". Please check the sensor connection.";
                } else {
                    float currentScore = calculateScore(temperature, humidity);

                    int sunriseHour = config["sunrise_hour"].toInt();
                    int sunsetHour = config["sunset_hour"].toInt();

                    Serial.println("🌡️ Room temperature is " + String(temperature) + "°C");
                    Serial.println("💧 Humidity level is " + String(humidity) + "%");
                    Serial.println("📊 Comfort score: " + String(currentScore));

                    // check if its day or night
                    float acOnScore;
                    float acOffScore;
                    if (currentHour >= sunriseHour &&
                        currentHour <= sunsetHour) {
                        Serial.println("🌞 Good day! Operating in daytime mode");
                        telegramLog +=
                            "\n🌞 Daytime comfort settings active (Hour: " + String(currentHour) + ":00)";
                        acOnScore =
                            truncf(config["ac_on_score_day"].toFloat() * 100) /
                            100;
                        acOffScore =
                            truncf(config["ac_off_score_day"].toFloat() * 100) /
                            100;
                    } else {
                        Serial.println("🌙 Good evening! Operating in nighttime mode");
                        telegramLog +=
                            "\n🌙 Nighttime comfort settings active (Hour: " + String(currentHour) + ":00)";
                        acOnScore =
                            truncf(config["ac_on_score_night"].toFloat() *
                                   100) /
                            100;
                        acOffScore =
                            truncf(config["ac_off_score_night"].toFloat() *
                                   100) /
                            100;
                    }

                    Serial.println("📊 Current comfort level: " + String(currentScore));
                    Serial.println("🔼 AC will turn on at: " + String(acOnScore));
                    Serial.println("🔽 AC will turn off at: " + String(acOffScore));
                    telegramLog +=
                        "\n🌡️ Temperature: " + String(temperature) +
                        "°C\n💧 Humidity: " + String(humidity) +
                        "%\n\n📊 Comfort Score: " + String(currentScore) +
                        "\n\n🔼 AC activation threshold: " + String(acOnScore) +
                        "\n🔽 AC deactivation threshold: " + String(acOffScore);

                    if (currentScore > acOnScore) {
                        if (isOnOff) {
                            if (acState != ON) {
                                Serial.println("🌡️ Room is getting warm - activating AC");
                                acState = ON;

                                Serial.println("⚡ Sending power signal to AC");
                                pressPowerButton();
                                beep();
                                alreadyWarningCount = 0;

                                telegramLog += "\n\n✨ AC has been activated for your comfort!";

                                acTurnOnAt = timeClient.getEpochTime();

                                // calculate how much time AC was off
                                if (acTurnOffAt > 0) {
                                    unsigned long acOffTime =
                                        acTurnOnAt - acTurnOffAt;
                                    int acOffTimeInMinutes = acOffTime / 60;
                                    telegramLog += "\n\n⏲️ AC was idle for " +
                                                   String(acOffTimeInMinutes) +
                                                   " minutes";
                                    note = "✨ Activating AC after " +
                                           String(acOffTimeInMinutes) +
                                           " minutes of rest";
                                } else {
                                    note = "✨ Activating AC for your comfort";
                                }
                            } else {
                                Serial.println("✅ AC is running normally");
                                telegramLog += "\n\n✅ AC is working to maintain comfort";
                                alreadyWarningCount++;

                                if (alreadyWarningCount >=
                                    maxAlreadyWarningCount) {
                                    telegramLog +=
                                        "\n\n⚠️ Room is still warm with AC on. Attempting to recalibrate...";
                                    alreadyWarningCount = 0;
                                    pressPowerButton();
                                    acState = ON;
                                    acTurnOnAt = timeClient.getEpochTime();
                                    note = "🔄 Recalibrating AC for better cooling";
                                }
                            }

                            telegramLog += "\n📉 " +
                                           String(acOffScore - currentScore) +
                                           " points until auto-shutdown";
                        } else {
                            telegramLog +=
                                "\n🔒 Room is warm but auto-control is disabled";
                        }
                    } else if (currentScore < acOffScore) {
                        if (acState != OFF) {
                            Serial.println("❄️ Room has reached comfortable temperature");
                            acState = OFF;

                            Serial.println("💤 Deactivating AC to save energy");
                            pressPowerButton();
                            beepTwice();
                            telegramLog += "\n\n✨ AC has been deactivated - room is comfortable!";
                            alreadyWarningCount = 0;

                            acTurnOffAt = timeClient.getEpochTime();

                            // calculate how much time AC was on
                            if (acTurnOnAt > 0) {
                                unsigned long acOnTime =
                                    acTurnOffAt - acTurnOnAt;
                                int acOnTimeInMinutes = acOnTime / 60;
                                telegramLog += "\n\n⏲️ AC was active for " +
                                               String(acOnTimeInMinutes) +
                                               " minutes";
                                note = "💤 Room is comfortable after " +
                                       String(acOnTimeInMinutes) + " minutes of cooling";
                            } else {
                                note = "💤 AC deactivated - room temperature is ideal";
                            }

                        } else {
                            Serial.println("✅ AC is off and room temperature is comfortable");
                            telegramLog += "\n\n✅ Room temperature remains comfortable";
                            alreadyWarningCount++;

                            if (alreadyWarningCount >= maxAlreadyWarningCount) {
                                telegramLog +=
                                    "\n\n❄️ Room might be getting too cool - adjusting...";
                                alreadyWarningCount = 0;
                                pressPowerButton();
                                acState = OFF;
                                acTurnOffAt = timeClient.getEpochTime();
                                note = "🌡️ Adjusting for optimal comfort";
                            }
                        }
                        telegramLog += "\n📈 " +
                                       String(acOnScore - currentScore) +
                                       " points until next cooling cycle";
                    } else {
                        Serial.println(
                            "✨ Perfect! Room temperature is in the comfort zone");
                        telegramLog +=
                            "\n\n✨ Everything is perfect! Room temperature is ideal.";
                        telegramLog += "\n📉 " +
                                       String(acOffScore - currentScore) +
                                       " points until AC deactivation";
                        telegramLog += "\n📈 " +
                                       String(acOnScore - currentScore) +
                                       " points until AC activation";
                    }

                    uploadDhtData(temperature, humidity, currentScore, note);
                }
            }
        }
    }

    int sleepTimeInMinutes = config["sleep_time_in_minutes"].toInt();
    Serial.println("💤 Taking a short break for " + String(sleepTimeInMinutes) +
                   " minutes");
    telegramLog +=
        "\n\n💤 System will check again in " + String(sleepTimeInMinutes) + " minutes...";
    logTelegram(telegramLog);
    int sleepTimeInMilliseconds = sleepTimeInMinutes * 60 * 1000;
    delay(sleepTimeInMilliseconds);
}

// Rest of the file remains unchanged