#include <ESPAsyncWebServer.h>

extern String global_password;
extern int global_alarm_threshold;
extern uint64_t  chipId;
extern String    ssid;
extern String    myHostname;
extern AsyncWebServer webServer;

void setupApi(void) {
    // ==================================================================
    // API CALL: get the device ID
    webServer.on("/api/id", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/id");
        request->send(200, "text/plain", ssid);
    });
    // ==================================================================
    // API CALL: get the current voltage
    webServer.on("/api/voltage", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/voltage");
        int sensorValue = analogRead(PIN_BAT_SENSE_ADC);
        request->send(200, "text/plain", String(sensorValue));
    });
    // ==================================================================
    // API CALL: get the current alarm voltage
    webServer.on("/api/alarm", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/alarm");
        request->send(200, "text/plain", String(global_alarm_threshold));
    });
    // ==================================================================
    // API CALL: set the current alarm voltage
    webServer.on("/api/alarm", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("POST /api/alarm");
        int headers = request->headers();
        if (request->hasParam("alarm", true) && request->hasHeader("Authorization")) {
            // check the password
            if (request->getHeader("Authorization")->value() == "Plaintext " + global_password) {
                // set the value in the global threshold variable
                AsyncWebParameter* p = request->getParam("alarm", true);
                global_alarm_threshold = p->value().toInt();
                // write the value to file to survive power cycles
                File alarm_file = SPIFFS.open("/alarm", "w");
                if (!alarm_file) {
                  Serial.println("Unable to open file for write: alarm");
                } else {
                  alarm_file.write(highByte(global_alarm_threshold));
                  alarm_file.write(lowByte(global_alarm_threshold));
                  alarm_file.close();
                }      
                Serial.println("ALARM VOLTAGE SET TO: " + p->value());
                request->send(200, "text/plain", "OK");
            } else {
                request->send(401); // Unauthorized
            }
        } else {
            request->send(422); // Missing alarm argument (or Authorization header)
        }
    });
    // ==================================================================
    // API CALL: validate the password
    webServer.on("/api/password", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/password");
        if (request->hasHeader("Authorization")) {
            Serial.println(request->getHeader("Authorization")->value());
            if (request->getHeader("Authorization")->value() == "Plaintext " + global_password) {
                request->send(200, "text/plain", "OK");        
            } else {
                request->send(403, "text/plain", "Forbidden");
            }
        } else {
            request->send(401, "text/plain", "Unauthorized");
        }
    });
    // ==================================================================
    // API CALL: set the password
    webServer.on("/api/password", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("POST /api/password");
        if (request->hasHeader("Authorization")) {
            Serial.println(request->getHeader("Authorization")->value());
            if (request->getHeader("Authorization")->value() == "Plaintext " + global_password) {
                if (request->hasParam("password", true)) {
                    global_password = request->getParam("password", true)->value();
                    Serial.println("write the password value to file");
                    File tmp_file = SPIFFS.open("/password", "w");
                    if (!tmp_file) {
                      Serial.println("Unable to open file for write: password");
                    } else {
                      tmp_file.write(global_password.c_str(), global_password.length());
                      tmp_file.close();
                    }
                    request->send(200, "text/plain", "OK");        
                } else {
                    request->send(422); // Missing password argument
                }
            } else {
                request->send(403, "text/plain", "Forbidden");
            }
        } else {
            request->send(401, "text/plain", String("Unauthorized"));
        }
    });
}
