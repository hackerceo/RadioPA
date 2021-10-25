#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


#define EVNT_HTTP_OTA 0


ESP8266WebServer webServer(80);
IPAddress apIP(172, 217, 28, 1);
//IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;

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
    delay(200);
    Serial.print("Setting soft-AP ... ");
    Serial.println(WiFi.softAP(ssid) ? "Ready" : "Failed!");
    delay(200);
    Serial.print("Soft-AP IP address = ");
    Serial.println(WiFi.softAPIP());

    
//    WiFi.softAP(ssid);
    delay(200); // https://github.com/espressif/arduino-esp32/issues/985


    // start mDNS service
    if (MDNS.begin(ssid.c_str())) {
        Serial.println("MDNS responder started");
    }

    // start DNS service
    dnsServer.start(53, "*", apIP);


    // configure web server
    const char * headerkeys[] = {"Host","Cookie","Accept-Encoding"};
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    // ask server to track these headers
    webServer.collectHeaders(headerkeys, headerkeyssize);

    // API CALL: get the device ID
    webServer.on("/api/id", [](){
        webServer.send(200, "text/plain", ssid);
    });

    // API CALL: get the current voltage
    webServer.on("/api/voltage", [](){
        int sensorValue = analogRead(PIN_BAT_SENSE_ADC);
        webServer.send(200, "text/plain", String(sensorValue));
    });

    // API CALL: get/set the current alarm voltage
    webServer.on("/api/alarm", [](){
        if (webServer.method() == HTTP_POST) {
          for (uint8_t i = 0; i < webServer.args(); i++) {
              if (webServer.argName(i) == "alarm") {
                  // set the value in the global threshold variable
                  global_alarm_threshold = String(webServer.arg(i)).toInt();
                  // write the value to file to survive power cycles
                  File alarm_file = SPIFFS.open("/alarm", "w");
                  if (!alarm_file) {
                    Serial.println("Unable to open file for write: alarm");
                  } else {
                    alarm_file.write(highByte(global_alarm_threshold));
                    alarm_file.write(lowByte(global_alarm_threshold));
                    alarm_file.close();
                  }      
                  Serial.println("ALARM VOLTAGE SET TO: " + webServer.arg(i));
                  break;
              }
          }
          webServer.send(200, "text/plain", "OK");
        } else {
          webServer.send(200, "text/plain", String(global_alarm_threshold));
        }
    });

    webServer.onNotFound([]() {
      // DEBUG: show headers *************************************************************
      String message ="";
      message += "Header Count: ";
      message += webServer.headers();
      message += "\n";
      for ( uint8_t i = 0; i < webServer.headers(); i++ ) {
        message += " " + webServer.headerName(i) + ": " + webServer.header(i) + "\n";
      }
      Serial.println(message);
      // *********************************************************************************
  
      // redirect if the host is not "my.milsim.prop"
      String requestedHost = String(webServer.header("Host"));
      requestedHost.toLowerCase();
      if (requestedHost != myHostname) {
        // do redirect
        webServer.sendHeader("Cache-Control","no-cache");
        webServer.sendHeader("Location", "http://" + myHostname + "/", true);
        webServer.send (307, "text/plain", "");
        // exit function
        return;
      }
      // correct host is targeted, process request normally
  
      // serve the static files from SPIFFS
      String location = webServer.uri();
      if (location == "/") location = "/index.html";
      Serial.println("location: "+location);
      String fileLoc = "/web" + location;
      File file;
      if(SPIFFS.exists(fileLoc + ".gz")) {
        Serial.println("using gzip file: " + fileLoc + ".gz");
        file = SPIFFS.open(fileLoc + ".gz", "r");
        // Content-Encoding header is automatically sent if file ends in ".gz"
        // and has a different MIME type when webServer.streamFile() is used
      } else {
        Serial.println("using standard file: " + fileLoc);
        file = SPIFFS.open(fileLoc, "r");
      }
      if(!file){
        Serial.println("Failed to open file for reading");
        webServer.send(404, "text/plain", "Not Found ("+fileLoc+")");
        return;
      } else {
        String fileMIME = "";
        if (fileLoc.endsWith(".html")) fileMIME = "text/html";
        if (fileLoc.endsWith(".css")) fileMIME = "text/css";
        if (fileLoc.endsWith(".js")) fileMIME = "text/javascript";
        if (fileLoc.endsWith(".png")) fileMIME = "image/png";
        if (fileLoc.endsWith(".jpg")) fileMIME = "image/jpeg";
        if (fileLoc.endsWith(".gif")) fileMIME = "image/gif";
  
        int sentBytes = webServer.streamFile(file, fileMIME);
        if (sentBytes != file.size()) {
          Serial.print("Partial send: ");
          Serial.print(sentBytes);
          Serial.print(" of ");
          Serial.println(file.size());
        } else {
          Serial.print("Full send: ");
          Serial.println(file.size());
        }
        file.close();
        return;
      }
    });

    // start webserver
    webServer.begin();

    
  },
  [](){
    dnsServer.processNextRequest();
    webServer.handleClient();
  }, 
  NULL
);


State stateNetwork_ota([](){
  Serial.println("Network:ota");
    SPIFFS.end(); // https://forum.arduino.cc/t/writing-reading-structs-from-spiffs-on-esp8266/574128/26
  }, 
  [](){
    ArduinoOTA.handle();
    dnsServer.processNextRequest();
    webServer.handleClient();
  }, 
  NULL
);

State stateNetwork_run(
  [](){
  Serial.println("Network:run");
  }, 
  [](){
    dnsServer.processNextRequest();
    webServer.handleClient();
  }, 
  NULL
);


// instantiate the finite state machine
Fsm fsmNetwork(&stateNetwork_startup);
























// ================================================================================================================== //
void networking_loop() {
    fsmNetwork.run_machine();
}


// ================================================================================================================== //
void networking_setup() {

    // transition automatically from startup to run state after a few seconds
    fsmNetwork.add_timed_transition(&stateNetwork_startup, &stateNetwork_run, 3000, NULL);
    // transition to OTA mode and join a network for network-based updating via Arduino IDE
    fsmNetwork.add_transition(&stateNetwork_startup, &stateNetwork_ota, EVNT_HTTP_OTA, NULL);


/*
    // Arduino OTA setup
    ArduinoOTA.setHostname("RadioPA");
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_FS
        type = "filesystem";
      }

      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
      Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
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

    
    // DNS Server
    dnsServer.start(53, "*", apIP);



//    server.on("/", HTTP_GET, []() {
//      server.sendHeader("Connection", "close");
//      server.send(200, "text/plain", "Brooklyn Tactical Radio PA version 0.1");
//    });

*/
}
