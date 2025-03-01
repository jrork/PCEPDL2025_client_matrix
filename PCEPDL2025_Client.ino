/**************************************************************************
*
*
*
***************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
// #include <FastLED.h>   //FastLED is conflicting with something and crashes when 
#include <Adafruit_NeoPixel.h>

#include "PCEPDL2025_Client.h"
#define NOOP 42

uint8_t unitID = 0;

const char* ssid ="ESPap";
const char* password = "thereisnospoon";
const char* mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int ticker = 0;                 // Ticker for time since last action

uint8_t gMode = NOOP;              // Keeps track of the mode for the LED displays

#define LED_COUNT 204
#define LED_PIN D2

// CRGB leds[NUM_LEDS];
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_BRG + NEO_KHZ800);

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
  strip.begin();                        // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();                         // Turn OFF all pixels ASAP
  strip.setBrightness(50);              // Set BRIGHTNESS to about 1/5 (max = 255)  
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
      turnOffStrip();
      break;
    case(1):
      turnOnBoardLED();
      stripOneColor();
      break;
    case(2):
      stripBrokenStrings();
      break;
    case(3):
      stripRainbow();
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

void turnOffStrip() {
  strip.clear();
  strip.show();
}

void noop() {

}

void stripOneColor() {
  uint32_t color = strip.Color(  0, 255,   0);
  strip.fill(color);
  strip.show();                          
  setMode(NOOP);
}

void stripBrokenStrings() {
  for(int i=0; i<15; i++)
  { 
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=15; i<20; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=20; i<45; i++)
  { 
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=46; i<50; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=50; i<80; i++)
  {
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=81; i<86; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=86; i<118; i++)
  {
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=118; i<123; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=123; i<148; i++)
  {
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=148; i<152; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=152; i<185; i++)
  {
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  for(int i=185; i<190; i++)
  {
    strip.setPixelColor(i, strip.Color(0,0,0));
  }
  for(int i=190; i<203; i++)
  {
    strip.setPixelColor(i, strip.Color(255,215,0));
  }
  strip.show();
  setMode(NOOP);
}

void stripRainbow() {
  for(long firstPixelHue = 0; firstPixelHue < 5*65536; firstPixelHue += 256) {
    // strip.rainbow() can take a single argument (first pixel hue) or
    // optionally a few extras: number of rainbow repetitions (default 1),
    // saturation and value (brightness) (both 0-255, similar to the
    // ColorHSV() function, default 255), and a true/false flag for whether
    // to apply gamma correction to provide 'truer' colors (default true).
    strip.rainbow(firstPixelHue);
    // Above line is equivalent to:
    // strip.rainbow(firstPixelHue, 1, 255, 255, true);
    // delay(wait);  // Pause for a moment
  }
  strip.show(); // Update strip with new contents
}

// WS2812B LED Strip switches Red and Green
// CRGB Scroll(int pos) {
// 	CRGB color (0,0,0);
// 	if(pos < 85) {
// 		color.g = 0;
// 		color.r = ((float)pos / 85.0f) * 255.0f;
// 		color.b = 255 - color.r;
// 	} else if(pos < 170) {
// 		color.g = ((float)(pos - 85) / 85.0f) * 255.0f;
// 		color.r = 255 - color.g;
// 		color.b = 0;
// 	} else if(pos < 256) {
// 		color.b = ((float)(pos - 170) / 85.0f) * 255.0f;
// 		color.g = 255 - color.b;
// 		color.r = 1;
// 	}
// 	return color;
// }

bool setMode(const uint8_t mode){
  snprintf(msg, MSG_BUFFER_SIZE, "Setting mode to %i", mode);
  Serial.println(msg);
  gMode = mode;
  return true;
}

