#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

class NetworkClient {

public:
    std::unique_ptr<BearSSL::WiFiClientSecure> httpClient;
    NetworkClient() {
        Serial.println("Creating NetworkClient...");
        httpClient.reset(new BearSSL::WiFiClientSecure);
        httpClient->setInsecure(); 
    }
};