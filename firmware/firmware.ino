#include <Fsm.h>
#include <ESP8266WiFi.h>
#include <FS.h>

// battery charge sensing
#define BAT_SENSE_THRESHOLD 1000 // (0-1024) ~ (0v - 3.3v)
#define BAT_SENSE_POLL_INTERVAL 15000 // every 15 seconds
#define PIN_BAT_SENSE_DRAIN 0
#define PIN_BAT_SENSE_ADC A0
#define EVNT_BATT_OK 0
#define EVNT_BATT_LOW 1 
int global_alarm_threshold;

String global_password;

// for neopixel lighting
#define PIN_WS2812 2

// Special effects channels (pyrotechs)
#define PIN_SFX1 5
#define PIN_SFX2 4

// communications with MP3-TF-16P module
#define PIN_MP3_RX 14
#define PIN_MP3_TX 12

// to amplifier
#define PIN_AUDIO 16 // is 13 in new boards


// ========== State Machine for Battery Monitoring ========== 

extern Fsm fsmBattery;

State stateBattery_startup([](){
    analogWriteFreq(200);
    analogWrite(PIN_AUDIO, 128);
    delay(500);
    analogWriteFreq(400);
    delay(250);
    analogWriteFreq(800);
    delay(500);
    analogWriteFreq(1600);
    delay(250);
    analogWriteFreq(3200);
    delay(250);
    analogWrite(PIN_AUDIO, 0);

    // default to 0
    global_alarm_threshold = 0;
    // read the alarm threshold from file ================================
    if (SPIFFS.exists("/alarm")) {
      Serial.println("read the alarm value from file");
      File tmp_file = SPIFFS.open("/alarm", "r");
      if (!tmp_file) {
        Serial.println("Unable to open file for read: alarm");
      } else {
        char readarray[2];
        tmp_file.readBytes(readarray, 2);
        global_alarm_threshold = word(readarray[0], readarray[1]);
        tmp_file.close();
      }
    } else {
      Serial.println("write the value to file");
      File tmp_file = SPIFFS.open("/alarm", "w");
      if (!tmp_file) {
        Serial.println("Unable to open file for write: alarm");
      } else {
        tmp_file.write(highByte(global_alarm_threshold));
        tmp_file.write(lowByte(global_alarm_threshold));
        tmp_file.close();
      }
    }
    // read the password from file =======================================
    global_password = String("password");
    if (SPIFFS.exists("/password")) {
      Serial.print("read the password from file: '");
      File tmp_file = SPIFFS.open("/password", "r");
      if (!tmp_file) {
        Serial.println("Unable to open file for read: password");
        Serial.println("Using default password: '" + global_password + "'");        
      } else {
        String readval = tmp_file.readString();
        global_password = readval;
        tmp_file.close();
        Serial.print(global_password);
        Serial.println("'");
      }
    } else {
      Serial.println("write the password value to file");
      File tmp_file = SPIFFS.open("/password", "w");
      if (!tmp_file) {
        Serial.println("Unable to open file for write: password");
      } else {
        tmp_file.write(global_password.c_str(), global_password.length());
        tmp_file.close();
      }
    }

    
    // switch to monitoring loop
    fsmBattery.trigger(EVNT_BATT_OK);
  }, 
  NULL, 
  NULL
);


State stateBattery_wait([](){
    Serial.println("Battery:wait");

  }, 
  NULL, 
  NULL
);
  
State stateBattery_check([](){
    Serial.println("Battery:check");
    // temporarly pull down the battery sense line
    pinMode(PIN_BAT_SENSE_DRAIN, OUTPUT);
    digitalWrite(PIN_BAT_SENSE_DRAIN, LOW);
    pinMode(PIN_BAT_SENSE_DRAIN, INPUT);
  },
  [](){
    // check the battry voltage
    int sensorValue = analogRead(PIN_BAT_SENSE_ADC);
    Serial.println(sensorValue);
    if (sensorValue < global_alarm_threshold) {
      fsmBattery.trigger(EVNT_BATT_LOW);
    } else {
      fsmBattery.trigger(EVNT_BATT_OK);
    }
  }, 
  NULL
);
State stateBattery_warn(
  [](){
    Serial.println("Battery:warn");
    analogWriteFreq(950);
    analogWrite(PIN_AUDIO, 128);
  }, 
  NULL, 
  [](){
    analogWriteFreq(750);
    delay(200);
    analogWrite(PIN_AUDIO, 0);
  }
);

Fsm fsmBattery(&stateBattery_startup);
extern Fsm fsmNetwork;

// ==========================================================





void setup() {
  Serial.begin(115200);
  Serial.println(" ");
  Serial.println("   [[ Radio-PA ]]");
  Serial.println("By Brooklyn Tactical");
  Serial.println(" (C)opyright 2021");
  Serial.println(" ");

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
  }
  
  // Battery Monitoring
  pinMode(PIN_BAT_SENSE_DRAIN, INPUT);
  fsmBattery.add_transition(&stateBattery_startup, &stateBattery_wait, EVNT_BATT_OK, NULL);
  fsmBattery.add_timed_transition(&stateBattery_wait, &stateBattery_check, BAT_SENSE_POLL_INTERVAL, NULL);
  fsmBattery.add_timed_transition(&stateBattery_warn, &stateBattery_wait, 500, NULL);
  fsmBattery.add_transition(&stateBattery_check, &stateBattery_warn, EVNT_BATT_LOW, NULL);
  fsmBattery.add_transition(&stateBattery_check, &stateBattery_wait, EVNT_BATT_OK, NULL);

  Serial.println("calling networking_setup()");
  networking_setup();
}

void loop() {
  fsmBattery.run_machine();
  networking_loop();
}
