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
        Serial.println("Servo is enabled");

        // servo
        powerButtonServo.attach(SERVO_PIN);
        delay(1000);
        powerButtonServo.write(90);
        delay(200);
        powerButtonServo.write(180);  // hands up
    } else {
        Serial.println("Servo is disabled");
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
            telegramLog += "🚨 config is empty";
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

            Serial.println("Current hour: " + String(currentHour));
            Serial.println("Should skip: " + String(shouldSkip));
            Serial.println("Is work hours enabled: " +
                           String(isWorkHoursEnabled));
            Serial.println("Work hour start: " + String(workHourStart));
            Serial.println("Work hour end: " + String(workHourEnd));
            Serial.println("Is outside work hours: " +
                           String(isOutsideWorkHours));

            String note = "";

            if (shouldSkip) {
                Serial.println("Skipping the process...");
                telegramLog += "\n\n🟠 Skipping the process...";
            } else if (isOutsideWorkHours) {
                Serial.println("Outside work hours...");
                telegramLog += "\n\n🟠 " + String(currentHour) +
                               " is outside working hours... skipped";
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
                        Serial.println("Temperature: " + String(temperature) + "C");
                        Serial.println("Humidity: " + String(humidity) + "%");
                        delay(5000);
                    }
                }

                if (isnan(temperature) || isnan(humidity)) {
                    Serial.println( "Temperature or humidity is NAN. Skipping...");
                    telegramLog += "\n\n🟠 Temperature or humidity is NAN. Temperature:" +  String(temperature) + ", Humidity:" + String(humidity);
                } else {
                    float currentScore = calculateScore(temperature, humidity);

                    int sunriseHour = config["sunrise_hour"].toInt();
                    int sunsetHour = config["sunset_hour"].toInt();

                    Serial.println("Temperature: " + String(temperature) + "C");
                    Serial.println("Humidity: " + String(humidity) + "%");
                    Serial.println("Score: " + String(currentScore));

                    // check if its day or night
                    float acOnScore;
                    float acOffScore;
                    if (currentHour >= sunriseHour &&
                        currentHour <= sunsetHour) {
                        Serial.println("Day time");
                        telegramLog +=
                            "\n🌞 Day time: Hour@" + String(currentHour);
                        acOnScore =
                            truncf(config["ac_on_score_day"].toFloat() * 100) /
                            100;
                        acOffScore =
                            truncf(config["ac_off_score_day"].toFloat() * 100) /
                            100;
                    } else {
                        Serial.println("Night time");
                        telegramLog +=
                            "\n🌚 Night time: Hour@" + String(currentHour);
                        acOnScore =
                            truncf(config["ac_on_score_night"].toFloat() *
                                   100) /
                            100;
                        acOffScore =
                            truncf(config["ac_off_score_night"].toFloat() *
                                   100) /
                            100;
                    }

                    Serial.println("Temp score: " + String(currentScore));
                    Serial.println("AC on score: " + String(acOnScore) +
                                   " or above");
                    Serial.println("AC off score: " + String(acOffScore) +
                                   " or below");
                    telegramLog +=
                        "\n☀️ Temperature: " + String(temperature) +
                        "C,\n💧 Humidity: " + String(humidity) +
                        ",\n\n📋 currentScore: " + String(currentScore) +
                        ",\n\n🔛 AC ON @: " + String(acOnScore) +
                        ",\n📴 AC OFF @: " + String(acOffScore);

                    if (currentScore > acOnScore) {
                        if (isOnOff) {
                            if (acState != ON) {
                                Serial.println("AC should be turned on!");
                                acState = ON;

                                Serial.println("Turning AC on...");
                                // Turn AC on
                                pressPowerButton();
                                beep();
                                alreadyWarningCount = 0;

                                telegramLog += "\n\n 🟢 AC turned on!";

                                acTurnOnAt = timeClient.getEpochTime();

                                // calculate how much time AC was off
                                if (acTurnOffAt > 0) {
                                    unsigned long acOffTime =
                                        acTurnOnAt - acTurnOffAt;
                                    int acOffTimeInMinutes = acOffTime / 60;
                                    telegramLog += "\n\n AC was off for " +
                                                   String(acOffTimeInMinutes) +
                                                   " minutes!";
                                    note = "🟢 Turning AC ON. Off duration: " +
                                           String(acOffTimeInMinutes) +
                                           " minutes!";
                                } else {
                                    note = "🟢 Turning AC ON.";
                                }
                            } else {
                                Serial.println("AC is already on...");
                                telegramLog += "\n\n 🟢 AC is already on!";
                                alreadyWarningCount++;

                                if (alreadyWarningCount >=
                                    maxAlreadyWarningCount) {
                                    telegramLog +=
                                        "\n\n🟠 AC is on, but its still hot! "
                                        "Turning "
                                        "ON AC again 🤔";
                                    alreadyWarningCount = 0;
                                    pressPowerButton();  // turn on one more
                                                         // time
                                    acState = ON;
                                    acTurnOnAt = timeClient.getEpochTime();
                                    note = "🟢🟢 Forcefully turning ON AC";
                                }
                            }

                            telegramLog += "\n Points to turn off " +
                                           String(acOffScore - currentScore) +
                                           " more!";
                        } else {
                            telegramLog +=
                                "\n 🥵 Temperature is high, but auto turn on "
                                "is "
                                "disabled!";
                        }
                    } else if (currentScore < acOffScore) {
                        if (acState != OFF) {
                            Serial.println("AC should be turned off!");
                            acState = OFF;

                            Serial.println("Turning AC off...");
                            // Turn AC off
                            pressPowerButton();
                            beepTwice();
                            telegramLog += "\n\n 🔴 AC turned OFF!";
                            alreadyWarningCount = 0;

                            acTurnOffAt = timeClient.getEpochTime();

                            // calculate how much time AC was on
                            if (acTurnOnAt > 0) {
                                unsigned long acOnTime =
                                    acTurnOffAt - acTurnOnAt;
                                int acOnTimeInMinutes = acOnTime / 60;
                                telegramLog += "\n\n AC was on for " +
                                               String(acOnTimeInMinutes) +
                                               " minutes!";
                                note = "🔴 Turning AC OFF. On duration: " +
                                       String(acOnTimeInMinutes) + " minutes!";
                            } else {
                                note = "🔴 Turning AC OFF.";
                            }

                        } else {
                            Serial.println("AC is already off...");
                            telegramLog += "\n\n🔴 AC is already off!";
                            alreadyWarningCount++;

                            if (alreadyWarningCount >= maxAlreadyWarningCount) {
                                telegramLog +=
                                    "\n\n🟠 AC is already off, but its still "
                                    "cold! "
                                    "Turning OFF AC again 🤔";
                                alreadyWarningCount = 0;
                                pressPowerButton();  // turn off one more time
                                acState = OFF;
                                acTurnOffAt = timeClient.getEpochTime();
                                note = "Forcefully turning OFF AC";
                            }
                        }
                        telegramLog += "\n Points to turn on " +
                                       String(acOnScore - currentScore) +
                                       " more!";
                    } else {
                        Serial.println(
                            "Temperature is within the acceptable range...");
                        telegramLog +=
                            "\n\n🟡 Temperature is within the acceptable "
                            "range!";
                        telegramLog += "\n Points to turn off " +
                                       String(acOffScore - currentScore) +
                                       " more!";
                        telegramLog += "\n Points to turn on " +
                                       String(acOnScore - currentScore) +
                                       " more!";
                    }

                    uploadDhtData(temperature, humidity, currentScore, note);
                }
            }
        }
    }

    int sleepTimeInMinutes = config["sleep_time_in_minutes"].toInt();
    Serial.println("Sleeping for " + String(sleepTimeInMinutes) +
                   " minutes...");
    telegramLog +=
        "\n\n 😴Sleeping for " + String(sleepTimeInMinutes) + " minutes...";
    logTelegram(telegramLog);
    int sleepTimeInMilliseconds = sleepTimeInMinutes * 60 * 1000;
    delay(sleepTimeInMilliseconds);
}


// Helper function to calculate Heat Index (simplified version for indoor use)
float calculateHeatIndex(float tempC, float humidity) {
    // For temperatures below 26°C, heat index has minimal effect
    if (tempC < 26.0) {
        return tempC + (humidity > 70.0 ? (humidity - 70.0) * 0.02 : 0);
    }
    
    // Convert to Fahrenheit for calculation
    float tempF = (tempC * 9.0 / 5.0) + 32.0;
    
    // Simplified heat index formula (Steadman's approximation)
    float heatIndexF = 0.5 * (tempF + 61.0 + ((tempF - 68.0) * 1.2) + (humidity * 0.094));
    
    // For higher temperatures, use more accurate formula
    if (heatIndexF > 80.0) {
        float T = tempF;
        float RH = humidity;
        
        heatIndexF = -42.379 + 
                    2.04901523 * T + 
                    10.14333127 * RH + 
                    -0.22475541 * T * RH + 
                    -0.00683783 * T * T + 
                    -0.05481717 * RH * RH + 
                    0.00122874 * T * T * RH + 
                    0.00085282 * T * RH * RH + 
                    -0.00000199 * T * T * RH * RH;
        
        // Adjustments for specific conditions
        if (RH < 13.0 && T >= 80.0 && T <= 112.0) {
            heatIndexF -= ((13.0 - RH) / 4.0) * sqrt((17.0 - abs(T - 95.0)) / 17.0);
        }
        if (RH > 85.0 && T >= 80.0 && T <= 87.0) {
            heatIndexF += ((RH - 85.0) / 10.0) * ((87.0 - T) / 5.0);
        }
    }
    
    // Convert back to Celsius
    return (heatIndexF - 32.0) * 5.0 / 9.0;
}

float calculateScore(float temperature, float humidity) {
    // Get config values
    float comfortTemperature = config["comfort_temperature"].toFloat();
    float comfortHumidity = config["comfort_humidity"].toFloat();
    float tempWeight = config["temperature_weight"].toFloat();
    float humidityWeight = config["humidity_weight"].toFloat();
    float tempThreshold = config["temperature_threshold"].toFloat();
    float humidityThreshold = config["humidity_threshold"].toFloat();
    
    // Calculate Heat Index for more realistic "feels like" temperature
    float feelsLikeTemp = calculateHeatIndex(temperature, humidity);
    
    float score = 0.0;
    
    // Temperature contribution using Heat Index (feels like temperature)
    if (feelsLikeTemp > comfortTemperature) {
        if (feelsLikeTemp > tempThreshold) {
            // Non-linear increase past threshold - heat becomes exponentially more uncomfortable
            score += tempWeight * (10 + pow((feelsLikeTemp - tempThreshold), 2));
        } else {
            score += tempWeight * (feelsLikeTemp - comfortTemperature);
        }
    }
    
    // Humidity contribution - high humidity makes it harder to cool down
    if (humidity > comfortHumidity) {
        if (humidity > humidityThreshold) {
            // Non-linear increase for high humidity - sweating becomes less effective
            score += humidityWeight * (5 + pow((humidity - humidityThreshold), 1.5));
        } else {
            score += humidityWeight * (humidity - comfortHumidity);
        }
    }
    
    // Additional realistic factors
    
    // Humidity amplification effect - very high humidity makes any heat much worse
    if (humidity > 75.0 && temperature > comfortTemperature) {
        float humidityAmplifier = (humidity - 75.0) / 25.0; // 0 to 1 scale
        score += tempWeight * (temperature - comfortTemperature) * humidityAmplifier;
    }
    
    // Low humidity relief - dry air feels slightly better even when hot
    if (humidity < 40.0 && temperature > comfortTemperature) {
        float dryAirRelief = (40.0 - humidity) / 40.0 * 0.3; // Small relief factor
        score *= (1.0 - dryAirRelief);
    }
    
    score = truncf(score * 100) / 100;
    return score;
}


void pressPowerButton() {
    // Press the power button
    Serial.println("Pressing the power button...");
    int handsDownAngle = config["hands_down_angle"].toInt();
    if (handsDownAngle == 0) {
        handsDownAngle = 0;
    }

    int handsUpAngle = config["hands_up_angle"].toInt();
    if (handsUpAngle == 0) {
        handsUpAngle = 180;
    }

    int upDownDelay = config["up_down_delay_in_ms"].toInt();
    if (upDownDelay == 0) {
        upDownDelay = 200;
    }

    Serial.println("Hands down angle: " + String(handsDownAngle));
    Serial.println("Hands up angle: " + String(handsUpAngle));
    Serial.println("Up down delay: " + String(upDownDelay));

    if(servoEnabled) {
        powerButtonServo.write(handsUpAngle);  // hands up
        delay(1000);
        powerButtonServo.write(handsDownAngle);  // hit bottom
        delay(upDownDelay);
        powerButtonServo.write(handsUpAngle);  // hands up
    } else {
        Serial.println("Servo is disabled, not pressing the button");
    }
}

void uploadDhtData(float temperature, float humidity, float score,
                   String note) {
    // Initializing an HTTPS communication using the secure client
    Serial.println("Connecting to Google Forms...");
    HTTPClient formRequest;
    if (formRequest.begin(*client.httpClient, GOOGLE_FORM_URL)) {  // HTTPS
        Serial.print("[HTTPS] POST...\n");

        formRequest.addHeader("Content-Type",
                              "application/x-www-form-urlencoded");

        // start connection and send HTTP header
        String httpRequestData = "entry.243518312=" + String(temperature) +
                                 "&entry.1071209622=" + String(score) +
                                 "&entry.1423375811=" + String(note) +
                                 "&entry.962580231=" + String(humidity);
        int httpCode = formRequest.POST(httpRequestData);
        // httpCode will be negative on error
        if (httpCode > 0) {
            // HTTP header has been send and Server response header has been
            // handled
            Serial.println("[HTTPS] POST... code: " + String(httpCode));
        } else {
            Serial.println(
                "[HTTPS] POST... failed, error: " + String(httpCode) + " - " +
                formRequest.errorToString(httpCode));
        }

        formRequest.end();
    } else {
        Serial.printf("[HTTPS] Unable to connect\n");
    }
}

String urlencode(String str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encodedString += '+';
        } else if (isalnum(c)) {
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
        yield();
    }
    return encodedString;
}

unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char)c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char)c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char)c - 'A' + 10);
    }
    return (0);
}

void logTelegram(String msg) {
    if (wifi.isConnected()) {
        // create an HTTPClient instance
        HTTPClient telegramSendMsgRequest;
        String url = "https://api.telegram.org/" + String(TELEGRAM_API_KEY) +
                     "/sendMessage?chat_id=-" + String(TELEGRAM_GROUP_ID) +
                     "&text=" + urlencode(msg);
        if (telegramSendMsgRequest.begin(*client.httpClient, url)) {  // HTTPS
            Serial.println("[HTTPS] GETing... " + msg);
            // start connection and send HTTP header
            int responseCode = telegramSendMsgRequest.GET();
            // responseCode will be negative on error
            if (responseCode > 0) {
                // HTTP header has been send and Server response header has been
                // handled
                Serial.println("[HTTPS] GET... code: " + String(responseCode));
            } else {
                Serial.println("[HTTPS] GET... failed, error: " +
                               String(responseCode));
            }

            telegramSendMsgRequest.end();
        } else {
            Serial.println("[HTTPS] Unable to connect");
        }
    }
}