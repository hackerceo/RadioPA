#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

String otaSsid;
String otaPass;

#define EVNT_OTA_START  0
#define EVNT_OTA_FAIL   1
#define EVNT_OTA_CONNECTED 2
#define OTA_UPDATING_FS   0
#define OTA_UPDATING_OS   1
int otaCurrentUpdate =-1;

AsyncWebServer webServer(80);

//ESP8266WebServer webServer(80);
IPAddress apIP(172, 217, 28, 1);
//IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;

extern String global_password;
extern int global_alarm_threshold;
uint64_t  chipId;
String    ssid;
String    myHostname;

// ========== State Machine for Battery Monitoring ========== 

extern Fsm fsmNetwork;

State stateNetwork_startup([](){
    Serial.println("Network:startup"); 
  
    // make AP name using chip id
    chipId = ESP.getChipId();
    Serial.print("Chip ID: "); Serial.println(chipId);
    ssid = String("RadioPA-");
    String unitId = String((uint16_t)chipId, HEX);
    unitId.toUpperCase();
    ssid.concat(unitId);
    Serial.println(ssid);
    myHostname = ssid + ".from.BrooklynTactical.com";
    myHostname.toLowerCase();
    Serial.println(myHostname);


    // start wifi
    WiFi.mode(WIFI_AP_STA);
    Serial.print("Setting soft-AP configuration ... ");
    Serial.println(WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)) ? "Ready" : "Failed!");
    delay(200);   // https://github.com/espressif/arduino-esp32/issues/985
    Serial.print("Setting soft-AP ... ");
    Serial.println(WiFi.softAP(ssid) ? "Ready" : "Failed!");
    delay(200);   // https://github.com/espressif/arduino-esp32/issues/985
    Serial.print("Soft-AP IP address = ");
    Serial.println(WiFi.softAPIP());


    // start mDNS service
    if (MDNS.begin(ssid.c_str())) {
        Serial.println("MDNS responder started");
        MDNS.addService("http","tcp",80);
    }


    // start DNS service
    dnsServer.start(53, "*", apIP);


    // TESTING!!! SERVE STATIC DIRECTORY
    webServer.serveStatic("/", SPIFFS, "/web/")
      .setDefaultFile("index.html");

THIS IS A TEST

    // API CALL: get the device ID
    webServer.on("/api/id", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/id");
        request->send(200, "text/plain", ssid);
    });

    // API CALL: get the current voltage
    webServer.on("/api/voltage", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/voltage");
        int sensorValue = analogRead(PIN_BAT_SENSE_ADC);
        request->send(200, "text/plain", String(sensorValue));
    });

    // API CALL: get the current alarm voltage
    webServer.on("/api/alarm", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("GET /api/alarm");
        request->send(200, "text/plain", String(global_alarm_threshold));
    });
    
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

    // API CALL: connect to a wifi network for OTA
    webServer.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("POST /api/ota");
        if (request->hasHeader("Authorization")) {
            Serial.println(request->getHeader("Authorization")->value());
            if (request->getHeader("Authorization")->value() == "Plaintext " + global_password) {
                if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
                    // save the wifi info to vars
                    otaSsid = request->getParam("ssid", true)->value();
                    otaPass = request->getParam("password", true)->value();
                    // switch into OTA mode
                    request->send(200, "text/plain", "OK");        
                    fsmNetwork.trigger(EVNT_OTA_START);
                } else {
                    request->send(422); // Missing WiFi info
                }
            } else {
                request->send(403, "text/plain", "Forbidden");
            }
        } else {
            request->send(401, "text/plain", String("Unauthorized"));
        }
    });

    webServer.onNotFound([](AsyncWebServerRequest *request){
      Serial.println(request->method() +" requested for " + request->host() + request->url() );
      // *********************************************************************************
  
      // redirect if the host is not "my.milsim.prop"
      String requestedHost = String(request->host());
      requestedHost.toLowerCase();
      if (requestedHost != myHostname) {
        // do redirect
        request->redirect("http://" + myHostname + "/");
        // exit function
        return;
      }
      // correct host is targeted, process request normally
  
      // serve the static files from SPIFFS
      String location = request->url();
      if (location == "/") location = "/index.html";
      Serial.println("location: "+location);
      String fileLoc = "/web" + location;

      String fileMIME = "";
      if (fileLoc.endsWith(".html")) fileMIME = "text/html";
      if (fileLoc.endsWith(".css")) fileMIME = "text/css";
      if (fileLoc.endsWith(".js")) fileMIME = "text/javascript";
      if (fileLoc.endsWith(".png")) fileMIME = "image/png";
      if (fileLoc.endsWith(".jpg")) fileMIME = "image/jpeg";
      if (fileLoc.endsWith(".gif")) fileMIME = "image/gif";

      AsyncWebServerResponse* response;

      if (SPIFFS.exists(fileLoc + ".gz")) {
        Serial.println("using gzip file: " + fileLoc + ".gz");
        response = request->beginResponse(SPIFFS, fileLoc + ".gz", fileMIME);
        Serial.println("TEST-A");
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Content-Disposition", "inline");
        Serial.println("TEST-B");
      } else {
        if (SPIFFS.exists(fileLoc)) {
          Serial.println("using standard file: " + fileLoc);
          response = request->beginResponse(SPIFFS, fileLoc, fileMIME);
        } else {
          response = request->beginResponse(404, "text/plain", "Not Found (" + fileLoc + ")");
        }
      }
      request->send(response);
      return;
    });

    // start webserver
    webServer.begin();    
  },
  [](){
    dnsServer.processNextRequest();
  }, 
  NULL
);


State stateNetwork_ota(
  [](){
    Serial.println("Network:ota");
    Serial.println("Connecting to host network using:");
    Serial.print("SSID:");
    Serial.println(otaSsid);
    Serial.print("Password:");
    Serial.println(otaPass);
//    ESP.wdtDisable();

//    Serial.println("Shutting down AP");
//    WiFi.softAPdisconnect(true);
//    WiFi.disconnect(true);
//    delay(3000);
    Serial.println("Starting Station mode");

//    WiFi.mode(WIFI_STA);
    WiFi.begin(otaSsid, otaPass);    
  }, 
  [](){
    dnsServer.processNextRequest();
    if (WiFi.status() == WL_CONNECTED) fsmNetwork.trigger(EVNT_OTA_CONNECTED);
  }, 
  [](){
    delay(500);
  }
);

State stateNetwork_ota_connected(
  [](){
    Serial.print("[CONNECTED] IP address: ");
    Serial.println(WiFi.localIP());
    // === Port defaults to 8266
    // ArduinoOTA.setPort(8266);
    // === make mdns respond to "RadioPA-[ChipID].local"
    ArduinoOTA.setHostname(ssid.c_str());
    // === No authentication by default
    // ArduinoOTA.setPassword("admin");
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            otaCurrentUpdate = OTA_UPDATING_OS;
            type = "sketch";
        } else { // U_FS
            otaCurrentUpdate = OTA_UPDATING_FS;
            type = "filesystem";
            // DO THIS??? "if updating FS this would be the place to unmount FS using FS.end()"
            SPIFFS.end(); // https://forum.arduino.cc/t/writing-reading-structs-from-spiffs-on-esp8266/574128/26
        }      
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
        if (otaCurrentUpdate == OTA_UPDATING_OS) {
            Serial.println("Rebooting...");
            ESP.restart();
        }
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();
    Serial.println("OTA is Ready");
  },
  [](){
    dnsServer.processNextRequest();
    ArduinoOTA.handle();
  },
  NULL
);

State stateNetwork_run(
  [](){
    Serial.println("Network:run");
  }, 
  [](){
    dnsServer.processNextRequest();
  }, 
  NULL
);


// instantiate the finite state machine
Fsm fsmNetwork(&stateNetwork_startup);


void networking_loop() {
    fsmNetwork.run_machine();
}

// ================================================================================================================== //
void networking_setup() {
    // transition automatically from startup to run state after a few seconds
    fsmNetwork.add_timed_transition(&stateNetwork_startup, &stateNetwork_run, 3000, NULL);
    // transition to OTA mode and join a network for network-based updating via Arduino IDE
    fsmNetwork.add_transition(&stateNetwork_run, &stateNetwork_ota, EVNT_OTA_START, NULL);
    fsmNetwork.add_transition(&stateNetwork_ota, &stateNetwork_run, EVNT_OTA_FAIL, NULL);
    fsmNetwork.add_transition(&stateNetwork_ota, &stateNetwork_ota_connected, EVNT_OTA_CONNECTED, NULL);
    
}
