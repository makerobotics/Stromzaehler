#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Ticker.h> // WDT
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define IP                211
#define DISPLAY_OLED        0
#define OLED_RESET          -1 // No reset pin
#define MQTT_CLIENT         "ESP8266Client_ZAEHLER"
#define CONSUMPTION         "room/consumption"
#define POWER               "room/power"
#define PERIODE             300000  //5mn
#define MAX_SML_SIZE        500

void setup_wifi();
void reconnect();
void callback(String topic, byte* message, unsigned int length);

void findStartSequence();
void findStopSequence();
void findPowerSequence();
void findConsumptionSequence();
void publishMessage();
void resetbuffer();
#if DISPLAY_OLED
void displayCustom(float consumption, float power);
void displayDebug(String message, int line);
#endif

int incomingByte = 0;
// Change the credentials below, so your ESP8266 connects to your router
const char* ssid_3 = "ssid_3";
const char* password_3 = "password_3";
const char* ssid_2 = "ssid_2";
const char* password_2 = "password_2";
const char* ssid_1 = "ssid_1";
const char* password_1 = "password_1";

// Change the variable to your Raspberry Pi IP address, so it connects to your MQTT broker
const char* mqtt_server = "192.168.2.201";

// WDT
const int wdtTimeout = 30;  //time in s to trigger the watchdog
Ticker secondTick;
volatile int watchdogCount = 0;

void ISRwatchdog() {
  watchdogCount++;
  if(watchdogCount > wdtTimeout) {
    Serial.println("Watchdog reset !!");
    ESP.reset();
  }
}

// Initializes the espClient. You should change the espClient name if you have multiple ESPs running in your home automation system
WiFiClient espClient;
PubSubClient client(espClient);

#if DISPLAY_OLED
Adafruit_SSD1306 display(OLED_RESET);
// Initialize the OLED display using i2c
// D2 -> SDA
// D1 -> SCL
#endif

byte inByte; //byte to store the serial buffer
byte smlMessage[1000]; //byte to store the parsed message
const byte startSequence[] = { 0x1B, 0x1B, 0x1B, 0x1B, 0x01, 0x01, 0x01, 0x01 }; //start sequence of SML protocol
const byte stopSequence[]  = { 0x1B, 0x1B, 0x1B, 0x1B, 0x1A }; //end sequence of SML protocol
const byte powerSequence[] =       { 0x07, 0x01, 0x00, 0x10, 0x07, 0x00, 0xFF, 0x01, 0x01, 0x62, 0x1B, 0x52, 0xFF, 0x55 }; //sequence preceeding the current "Wirkleistung" value (4 Bytes)
const byte consumptionSequence[] = { 0x07, 0x01, 0x00, 0x01, 0x08, 0x01, 0xFF, 0x01, 0x01, 0x62, 0x1E, 0x52, 0xFF, 0x56 }; //sequence predeecing the current "Gesamtverbrauch" value (8 Bytes)
int smlIndex; //index counter within smlMessage array
int startIndex; //start index for start sequence search
int stopIndex; //start index for stop sequence search
int stage; //index to maneuver through cases
byte power[4]; //array that holds the extracted 4 byte "Wirkleistung" value
byte consumption[8]; //array that holds the extracted 8 byte "Gesamtverbrauch" value
int currentpower; //variable to hold translated "Wirkleistung" value
uint64 currentconsumption; //variable to hold translated "Gesamtverbrauch" value
float currentconsumptionkWh; //variable to calulate actual "Gesamtverbrauch" in kWh
float currentpowerWatt; //variable to calulate actual "Verbrauch" in Watt
long now = millis();
long lastTick = 0;

void setup() {
    Serial.begin(9600);
#if DISPLAY_OLED
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C
    // Clear the display buffer.
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.clearDisplay();
    displayDebug("Setup", 1);
#endif
    setup_wifi();
    
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // WDT
    secondTick.attach(1, ISRwatchdog);
}

void loop() {
    // WDT
    watchdogCount = 0; //reset timer (feed watchdog)

  if( (WiFi.status() == WL_CONNECTED) ) {
      
    ArduinoOTA.handle();
      
    now = millis();

//    if(now > (1000*60*60)) ESP.restart();

    if (!client.connected()) {
        reconnect();
    }
    if(!client.loop())
        client.connect(MQTT_CLIENT);

    switch (stage) {
    case 0:
      findStartSequence(); // look for start sequence
      break;
    case 1:
      findStopSequence(); // look for stop sequence
      break;
    case 2:
      findPowerSequence(); //look for power sequence and extract
      break;
    case 3:
      findConsumptionSequence(); //look for consumption sequence and exctract
      break;
    case 4:
      publishMessage(); // do something with the result
      break;
    }
  }
}

// Don't change the function below. This functions connects your ESP8266 to your router
void setup_wifi() {
    IPAddress ip(192, 168, 2, IP);
    IPAddress gateway(192, 168, 2, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(ip, gateway, subnet);

    for(int i=1;i<=3;i++)
    {
        int count = 0;
        delay(10);

        // We start by connecting to a WiFi network
        Serial.println();
        Serial.print("Connecting to ");
        if(i==1){
            Serial.println(ssid_1);
            WiFi.begin(ssid_1, password_1);
        }
        else if(i==2){
            Serial.println(ssid_2);
            WiFi.begin(ssid_2, password_2);
        }
        else{
            Serial.println(ssid_3);
            WiFi.begin(ssid_3, password_3);
        }
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            count++;
            if(count == 10){
                count = 0;
                break;
            }
        }
        if(WiFi.status() == WL_CONNECTED) break;
    }
    Serial.println("");
    Serial.print("WiFi connected - ESP IP address: ");
    Serial.println(WiFi.localIP());
}

// This functions is executed when some device publishes a message to a topic that your ESP8266 is subscribed to
void callback(String topic, byte* message, unsigned int length) {
  //Serial.print("Message arrived on topic: ");
  //Serial.print(topic);
  //Serial.print(". Message: ");
  String messageTemp;

  for (unsigned int i = 0; i < length; i++) {
    //Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  //Serial.println();
  //Serial.println();
}

// This functions reconnects your ESP8266 to your MQTT broker
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    //Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    /* YOU MIGHT NEED TO CHANGE THIS LINE, IF YOU'RE HAVING PROBLEMS WITH MQTT MULTIPLE CONNECTIONS */
    if (client.connect(MQTT_CLIENT)) {
      //Serial.println("connected");
      // Subscribe or resubscribe to a topic
      // You can subscribe to more topics (to control more LEDs in this example)
      //client.subscribe(LAMP);
    } else {
#if DISPLAY_OLED
        displayDebug("MQTT", 2);
        displayDebug("Failed", 3);
#endif
      //Serial.print("failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

#if DISPLAY_OLED
void displayCustom(float consumption, float power){
    static int i = 0;
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.clearDisplay();

    display.setCursor(0,0);
    display.println(String(consumption) + " kwh");
    display.setCursor(0,10);
    display.println(String(power) + " W");
    display.setCursor(0,20);
    //display.println("Iteration: " + String(i));
    display.println(WiFi.localIP());
    display.display();
    i++;
}

void displayDebug(String message, int line){
    static String L1 = "", L2 = "", L3 = "";

    if(line == 1) L1 = message;
    else if(line == 2) L2 = message;
    else L3 = message;

    display.clearDisplay();
    display.setCursor(0,0);
    display.println(L1);
    display.setCursor(0,10);
    display.println(L2);
    display.setCursor(0,20);
    display.println(L3);
    display.display();
}
#endif

void findStartSequence() {
  while (Serial.available())
  {
    inByte = Serial.read(); //read serial buffer into array
    if (inByte == startSequence[startIndex]) //in case byte in array matches the start sequence at position 0,1,2...
    {
      smlMessage[startIndex] = inByte; //set smlMessage element at position 0,1,2 to inByte value
      startIndex++;
      if (startIndex == sizeof(startSequence)) //all start sequence values have been identified
      {
     //   Serial.println("Match found");
        stage = 1; //go to next case
        smlIndex = startIndex; //set start index to last position to avoid rerunning the first numbers in end sequence search
        startIndex = 0;
      }
    }
    else {
      startIndex = 0;
    }
  }
}

void resetbuffer() {
    // clear the buffers
    memset(smlMessage, 0, sizeof(smlMessage));
    memset(power, 0, sizeof(power));
    memset(consumption, 0, sizeof(consumption));
    //reset case
    smlIndex = 0;
    stage = 0; // start over
}

void findStopSequence() {
  while (Serial.available())
  {
    inByte = Serial.read();
    smlMessage[smlIndex] = inByte;
    smlIndex++;

    if (inByte == stopSequence[stopIndex])
    {
      stopIndex++;
      if (stopIndex == sizeof(stopSequence))
      {
        stage = 2;
        stopIndex = 0;
        //displayDebug("find stop", 2);
      }
    }
    else {
      stopIndex = 0;
    }

    // New check for deadlock
    if (smlIndex >= MAX_SML_SIZE){
        resetbuffer();
        break;
    }
  }
}

void findPowerSequence() {
  byte temp; //temp variable to store loop search data
 startIndex = 0; //start at position 0 of exctracted SML message

for(unsigned int x = 0; x < sizeof(smlMessage); x++){ //for as long there are element in the exctracted SML message
    temp = smlMessage[x]; //set temp variable to 0,1,2 element in extracted SML message
    if (temp == powerSequence[startIndex]) //compare with power sequence
    {
      startIndex++;
      if (startIndex == sizeof(powerSequence)) //in complete sequence is found
      {
        for(int y = 0; y< 4; y++){ //read the next 4 bytes (the actual power value)
          power[y] = smlMessage[x+y+1]; //store into power array
        }
        stage = 3; // go to stage 3
        startIndex = 0;
      }
    }
    else {
      startIndex = 0;
    }
  }
   currentpower = (power[0] << 24 | power[1] << 16 | power[2] << 8 | power[3]); //merge 4 bytes into single variable to calculate power value
   currentpowerWatt = (float)currentpower/10.0;
}


void findConsumptionSequence() {
  byte temp;

  startIndex = 0;
for(unsigned int x = 0; x < sizeof(smlMessage); x++){
    temp = smlMessage[x];
    if (temp == consumptionSequence[startIndex])
    {
      startIndex++;
      if (startIndex == sizeof(consumptionSequence))
      {
        for(int y = 0; y< 5; y++){
          //hier muss für die folgenden 5 Bytes hoch gezählt werden
          consumption[y] = smlMessage[x+y+1];
        }
        stage = 4;
        startIndex = 0;
      }
    }
    else {
      startIndex = 0;
    }
  }

   currentconsumption = (consumption[0] << 32 | consumption[1] << 24 | consumption[2] << 16 | consumption[3] << 8 | consumption[4]); //combine and turn 8 bytes into one variable
   currentconsumptionkWh = (float)currentconsumption/10000.0; // 10.000 impulses per kWh
}


void publishMessage() {
#if DISPLAY_OLED
    displayCustom(currentconsumptionkWh, currentpowerWatt);
#endif

    if ((now - lastTick) > PERIODE) {
        lastTick = now;
        static char currentcon[21];
        dtostrf(currentconsumptionkWh, 20, 5, currentcon);
        client.publish(CONSUMPTION, currentcon);
    }
    static char currentpow[7];
    dtostrf(currentpowerWatt, 6, 2, currentpow);
    client.publish(POWER, currentpow);

    // clear the buffers
    resetbuffer();
}
