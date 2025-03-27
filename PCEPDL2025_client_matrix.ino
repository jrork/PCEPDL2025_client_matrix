/*************************************************************************************
*                             PCEPDL2025 Client
*  This client is to be used for the PCEP 2025 Drumline competition.  This code is 
*  1 of 5 parts needed for the entire project:
*  - PCEPDL2025_Client - runs on an ESP8266 and is wired directly to the LED panels.
*  - PECPDL2025_Commander - Connects to MQTT Broker and sends mode commands
*  - PCEPDL2025_MQTT_Broker - acts as MQTT broker and WiFi Access Point
*  - PCEPDL2025_EEPROM_Write - Writes the ID values to the individual clients
*
*  @Author - Joseph Rork
*  @Author - Richard Dryja
*
**************************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_GFX.h>


#include "PCEPDL2025_Client.h"
#define NOOP 42                 // Randomly picked 42, just needs to be a number not represented in loop()


uint8_t unitID = 0;  // Initally set to 0, updated from EEPROM

const char* ssid ="ESPap";
const char* password = "thereisnospoon";
const char* mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int ticker = 0;                    // Ticker for time since last action

uint8_t gMode = NOOP;              // Keeps track of the mode for the LED displays

#define LED_PIN D2
#define MATRIX_HEIGHT 6
#define MATRIX_WIDTH 34
#define NUM_LEDS (MATRIX_HEIGHT * MATRIX_WIDTH)
uint8_t BRIGHTNESS = 255;

#define RAINBOW_SPEED 500       // Change this to modify the speed at which the rainbow changes
#define SEGMENT_COUNT 3         // Change this to divide up the entire rainbow into segments.  1 = entire rainbow shown at once
#define NUM_OF_HUES 65535       // This is just for readability.  This is the number of values in a 16-bit number
#define FRAMES_PER_SECOND 60 // How fast we want to show our frames

unsigned long lastFrameTime = 0;   // Keeps track of the time the last rainbow frame was drawn

// Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(MATRIX_WIDTH, MATRIX_HEIGHT, LED_PIN,
  NEO_MATRIX_BOTTOM     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_RGB            + NEO_KHZ800);

uint16_t indexHue = 0;                                 // Keep track of this externally to ensure non-blocking code
// uint32_t singleColorValue = strip.Color(0, 255, 0);   // Single color value updated via JSON message
const uint32_t singleColorValue = matrix.Color(255, 215, 0);   // Single color value updated via JSON message

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());              // WTF does this do?

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());    
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // find out which unit the message is for.  NOTE: 0 is a message for all
  String topicStr(topic);
  int messageUnitID = 0; // initialize to 0 to default 
  if (topicStr.indexOf('/') >= 0) {
    // The topic includes a '/', we'll try to read the unitID after that
    topicStr.remove(0, topicStr.indexOf('/')+1);
    // Now see if there's a unitID after the '/'
    messageUnitID = topicStr.toInt();
    // Check to see if the message was directly for us (unitID) or global (0)
    if ((messageUnitID == 0) || (messageUnitID == unitID)) {
      // Handle the message below this line...
      // Convert payload to string
      String message = "";
      for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
      }
      Serial.print("Message arrived on topic: ");
      Serial.println(topic);
      Serial.print("Message: ");
      Serial.println(message);

      // Parse JSON
      StaticJsonDocument<200> jsonDoc;
      DeserializationError error = deserializeJson(jsonDoc, message);

      if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        snprintf (msg, MSG_BUFFER_SIZE, "Unit %i was unable to parse the JSON message", unitID);
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish("status", msg);
        return;
      }

      // Access JSON data
      if (jsonDoc.containsKey("mode")) {
        const uint8_t modeValue = jsonDoc["mode"]; 
        Serial.print("Parsed mode value: ");
        Serial.println(modeValue);
        setMode(modeValue);
      }

      if (jsonDoc.containsKey("color")) {
        const uint32_t colorValue = strtoul(jsonDoc["color"], nullptr, 16); 
        Serial.print("Parsed color value: 0x");
        Serial.println(colorValue, HEX);
        setSingleColorValue(colorValue);
      }
      if (jsonDoc.containsKey("offset")) {
        const uint8_t offsetValue = jsonDoc["offset"]; 
        Serial.print("Parsed offset value: ");
        Serial.println(offsetValue);
      }
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      const char* subTopic = "command/+";
      client.subscribe(subTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  pinMode(BUILTIN_LED, OUTPUT);         // Initialize the BUILTIN_LED pin as an output
  Serial.begin(74880);                  // Start serial at the speed that prints out ESP8266 bootup messages
  getUnitID();                          // Retrieve the UnitID from EEPROM
  setup_wifi();                         // Connect to the WiFi of the MQTT Broker
  client.setServer(mqtt_server, 1883);  // Connect to the MQTT Broker
  client.setCallback(callback);         // Register callback() as the callback funciton for messages from Broker
  // strip.begin();                        // INITIALIZE NeoPixel strip object (REQUIRED)
  // strip.show();                         // Turn OFF all pixels ASAP
  // strip.setBrightness(50);              // Set BRIGHTNESS to about 1/5 (max = 255)  
  matrix.begin();
  matrix.clear();
  matrix.show();
  Serial.println("Setup Complete");
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  sendKeepalive();

  switch(gMode){
    case(0):
      turnOffBoardLED();
      turnOffMatrix();
      break;
    case(1):
      turnOnBoardLED();
      matrixOneColor();
      break;
    case(2):
      matrixBrokenStrings();
      break;
    case(3):
      rainbow();
      break;
    case(4):
      break;
    case(5):
      break;
    case(6):
      break;
    case(7):
      break;
    case(8):
      break;
    case(9):
      break;
    case(10):
      break;
    case(11):
      break;
    case(12):
      break;
    case(13):
      break;
    case(14):
        break;
    case(15):
        break;
    case(16):
        break;
    case(17):
        break;
    case(NOOP):
    default:
      noop();
      break;
  }
}

void getUnitID() {
  uint8_t* unitIdBytes = reinterpret_cast<uint8_t*>(&unitID);
  const uint8_t unitIdSize = sizeof(unitID);
  EEPROM.begin(unitIdSize);

  for(int index = 0; index < unitIdSize; index++){
    unitIdBytes[index] = EEPROM.read(index);
  }
  memcpy(&unitID, unitIdBytes, sizeof(unitID));
  Serial.println();
  Serial.print("unitID is: ");
  Serial.println(unitID);
}

// Check to ensure the message sent is one we are expecting
bool checkMessageValiditiy(char message){
  return true;
}

void sendKeepalive() {
  unsigned long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    ++ticker;
    snprintf (msg, MSG_BUFFER_SIZE, "Unit %i says,hello world #%ld", unitID, ticker);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish("status", msg);
  }
}

void turnOnBoardLED() {
  Serial.println("Turning on Board LED");
  digitalWrite(BUILTIN_LED, false);
  setMode(NOOP);
}

void toggleBoardLED() {
  Serial.println("Toggling Board LED");
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
  setMode(NOOP);
}

void turnOffBoardLED(){
  Serial.println("Turning off Board LED");
  digitalWrite(BUILTIN_LED, true);
  setMode(NOOP);
}

void turnOffMatrix() {
  matrix.clear();
  matrix.show();
}

void noop() {
  // Just chill.  Don't do anything.  Relax.  This is a 'No Operation'...
}

// void matrixOneColor() {
//   uint16_t color = matrix.Color(255, 215, 0);   // Single color value updated via JSON message
//   matrixOneColor(color);
// }

// void matrixOneColor(uint8_t red, uint8_t green, uint8_t blue) {
//   matrixOneColor(matrix.Color(red, green, blue));
// }

void matrixOneColor() {
  matrix.fillScreen(matrix.Color(255, 215, 0));
  matrix.show();                          
  setMode(NOOP);
}

void setSingleColorValue(uint16_t color) {
  // singleColorValue = color;
}

void matrixBrokenStrings() {
  matrixOneColor();
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    const uint8_t breakSize = random(3, 8);
    const uint8_t randomStart = random(8, 25);
    for (uint8_t x = randomStart; x < randomStart + breakSize; x++) {
      if (x == MATRIX_WIDTH) break;   //bail out if we get past the end of the row but this should never happen
      matrix.drawPixel(x, y, matrix.Color(0,0,0));
    }
  }
  matrix.show();
  setMode(NOOP);
  // for(int i=0; i<15; i++)
  // { 
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=15; i<20; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=20; i<45; i++)
  // { 
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=46; i<50; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=50; i<80; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=81; i<86; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=86; i<118; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=118; i<123; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=123; i<148; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=148; i<152; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=152; i<185; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // for(int i=185; i<190; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(0,0,0));
  // }
  // for(int i=190; i<203; i++)
  // {
  //   strip.setPixelColor(i, strip.Color(255,215,0));
  // }
  // strip.show();
  // setMode(NOOP);
}

void rainbow() {
  unsigned long now = millis();
  // if (now - lastFrameTime > (1000/FRAMES_PER_SECOND)) {
  // if (now - lastFrameTime > 100) {
  //   lastFrameTime = now;
    // indexHue = indexHue + RAINBOW_SPEED;  // This is a 16-bit number which will automatically roll over to 0 after 65535
    indexHue++;
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        matrix.drawPixel(x, y, matrix.ColorHSV(indexHue));
        // matrix.drawPixel(x, y, matrix.ColorHSV(indexHue + (x * (NUM_OF_HUES/NUM_LEDS)/SEGMENT_COUNT)));
      }
    }
    matrix.show();
    delay(50);
  // }
  // for (uint8_t i = 0; i < NUM_LEDS; i++)
  // {
  //   uint32_t color = strip.ColorHSV(indexHue + (i* (NUM_OF_HUES/NUM_LEDS)/SEGMENT_COUNT));
  //   strip.setPixelColor(i, color);
  // }
  // strip.show();
}

void checkMatrix() {
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      matrix.drawPixel(x, y, matrix.Color(0,0,255));
      matrix.show();
      delay(100);

      matrix.drawPixel(x,y,matrix.Color(0,0,0));
      matrix.show();
      delay(100);
    }
  }
}

bool setMode(const uint8_t mode){
  snprintf(msg, MSG_BUFFER_SIZE, "Setting mode to %i", mode);
  Serial.println(msg);
  gMode = mode;
  return true;
}

uint16_t color24to16(uint32_t color) {
  return ((uint16_t)(((color & 0xFF0000) >> 16) & 0xF8) << 8) |
         ((uint16_t)(((color & 0x00FF00) >>  8) & 0xFC) << 3) |
                    (((color & 0x0000FF) >>  0)         >> 3);
}
