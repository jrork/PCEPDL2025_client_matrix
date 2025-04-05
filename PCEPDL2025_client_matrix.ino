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

#define NOOP 42                 // Randomly picked 42, just needs to be a number not represented in loop()
#define FADE_OUT 17             // Randomly picked 17.  matrixFadeOut has an initial save that occurs at mode 4 and then iterates at mode 17

uint8_t unitID = 0;             // Initally set to 0, updated from EEPROM

const char* ssid ="ESPap";
const char* password = "thereisnospoon";
const char* mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int ticker = 0;                    // Ticker for time since last heartbeat message

uint8_t gMode = NOOP;              // Keeps track of the mode for the LED displays

#define LED_PIN D2
#define MATRIX_HEIGHT 6
#define MATRIX_WIDTH 34
#define MATRIX_SIZE (MATRIX_HEIGHT * MATRIX_WIDTH)

// Values for rainbow()
#define RAINBOW_SPEED 500          // Change this to modify the speed at which the rainbow changes
#define SEGMENT_COUNT 5            // Change this to divide up the entire rainbow into segments.  1 = entire rainbow shown at once
#define NUM_OF_HUES 65535          // This is just for readability.  This is the number of values in a 16-bit number
#define FRAMES_PER_SECOND 60       // How fast we want to show our frames
unsigned long lastFrameTime = 0;   // Keeps track of the time the last rainbow frame was drawn
uint16_t indexHue = 0;             // Keep track of this externally to ensure non-blocking code
#define hueWidthPerPanel NUM_OF_HUES/(MATRIX_WIDTH*SEGMENT_COUNT)
uint16_t panelOffset = (NUM_OF_HUES/SEGMENT_COUNT)*(unitID-1);

// Values for matrixFadeIn()
#define FADE_IN_TIME 10000         // 10,000 ms for 10 sec fade
unsigned long lastFadeUpTime = 0;
uint8_t upBrightness = 0;

// Values for matrixFadeOut()
#define FADE_OUT_TIME 10000
uint32_t originalColors[MATRIX_SIZE];        // Array to store each pixel's original color.
unsigned long fadeStartTime = 0;             // Time when the fade started.
unsigned long lastFadeUpdate = 0;            // Track time between updates to the fade 
const unsigned long fadeUpdateInterval = 50; // Update fade every 50ms.

// Gamma correction lookup table for gamma = 2.8
const uint8_t gamma8[] PROGMEM = {
  0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7,
  8, 9,10,11,12,13,14,15,16,17,18,19,21,22,23,24,
  26,27,28,30,31,33,34,36,37,39,41,42,44,46,48,49,
  51,53,55,57,59,61,63,65,67,69,71,74,76,78,81,83,
  86,88,91,94,96,99,102,105,108,111,114,117,120,123,127,130,
  133,137,140,144,147,151,155,159,163,167,171,175,179,183,188,192,
  197,201,206,211,216,221,226,231,236,241,246,251,255
  // Make sure there are 256 values in total.
};

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(MATRIX_WIDTH, MATRIX_HEIGHT, LED_PIN,
  NEO_MATRIX_BOTTOM     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_RGB            + NEO_KHZ800);

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
  recalculatePanelOffset();             // Recalulates the panelOffset with new UnitID as an input
  setup_wifi();                         // Connect to the WiFi of the MQTT Broker
  client.setServer(mqtt_server, 1883);  // Connect to the MQTT Broker
  client.setCallback(callback);         // Register callback() as the callback funciton for messages from Broker
  matrix.begin();                       // Initialize the matrix
  matrix.clear();                       // Clear the matrix
  matrix.show();                        // Display the cleared matrix
  Serial.println("Setup Complete");
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  sendKeepAlive();

  switch(gMode){
    case(0):
      turnOffMatrix();
      resetToDefaults();
      break;
    case(1):
      matrixFadeIn();
      break;
    case(2):
      matrixBrokenStrings();
      break;
    case(3):
      storeOriginalColors();
      break;
    case(4):
      gold();
      break;
    case(5):
      rainbow();
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
      matrixFadeOut(FADE_OUT_TIME);
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

void recalculatePanelOffset() {
  panelOffset = (NUM_OF_HUES/SEGMENT_COUNT)*(unitID-1);
  Serial.print("\npanelOffset: ");
  Serial.print(panelOffset);
}

// Check to ensure the message sent is one we are expecting
bool checkMessageValiditiy(char message){
  return true;
}

void sendKeepAlive() {
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

void resetToDefaults() {
  upBrightness = 0;
  indexHue = 0;
}


void matrixFadeIn() {
  if (upBrightness < 255) {
    unsigned long now = millis();
    if (now - lastFadeUpTime > (FADE_IN_TIME/255)) {     //not sure why i'm dividing by 255?
      lastFadeUpTime = now;
      upBrightness++;
      matrix.fill(matrix.ColorHSV(180, 0, upBrightness));  //first value doesn't matter because saturation is 0 i.e. white
      matrix.show();
    }
  }
  else {
    matrix.fill(matrix.ColorHSV(180, 0, 255));
    matrix.show();
  }
}

// Call this function once before starting the fade-out to capture the original colors.
void storeOriginalColors() {
  for (uint16_t i = 0; i < MATRIX_SIZE; i++) {
    originalColors[i] = matrix.getPixelColor(i);
  }
  fadeStartTime = millis();
  setMode(FADE_OUT);
}

void matrixFadeOut(unsigned long fadeDuration) {
  unsigned long now = millis();

  // Throttle updates: run the fade code only every fadeUpdateInterval.
  if (now - lastFadeUpdate < fadeUpdateInterval) {
    return;
  }
  lastFadeUpdate = now;
  
  // Calculate elapsed time since fade start.
  unsigned long elapsed = now - fadeStartTime;
  if (elapsed > fadeDuration) {
    elapsed = fadeDuration;
  }
  
  // Calculate brightness factor: 1.0 at start, 0.0 at end.
  float factor = float(fadeDuration - elapsed) / fadeDuration;
  
  // Process each pixel.
  for (uint16_t i = 0; i < MATRIX_SIZE; i++) {
    uint32_t origColor = originalColors[i];
    
    // Extract red, green, and blue components.
    uint8_t r = (origColor >> 16) & 0xFF;
    uint8_t g = (origColor >> 8) & 0xFF;
    uint8_t b = origColor & 0xFF;
    
    // Apply brightness scaling.
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    
    // Apply gamma correction via the lookup table.
    uint8_t newR = pgm_read_byte(&gamma8[r]);
    uint8_t newG = pgm_read_byte(&gamma8[g]);
    uint8_t newB = pgm_read_byte(&gamma8[b]);
    
    // Recombine and set the new pixel color.
    uint32_t newColor = matrix.Color(newR, newG, newB);
    matrix.setPixelColor(i, newColor);
  }
  matrix.show();
}

void matrixOneColor() {  
  matrix.clear();
  matrix.fillScreen(matrix.Color(255, 255, 255));
  matrix.show();
  setMode(NOOP);
}

void setSingleColorValue(uint16_t color) {
  // singleColorValue = color;
}

void matrixBrokenStrings() {
  matrix.clear();
  matrix.fillScreen(matrix.Color(255, 255, 255));
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    const uint8_t breakSize = random(3, 8);
    const uint8_t randomStart = random(8, 25);
    for (uint8_t x = randomStart; x < randomStart + breakSize; x++) {
      if (x >= MATRIX_WIDTH) break;   //bail out if we get past the end of the row but this should never happen
      matrix.drawPixel(x, y, matrix.Color(0,0,0));
    }
  }
  matrix.show();
  setMode(NOOP);
}

void rainbow() {
  unsigned long now = millis();
  if (now - lastFrameTime > 10) {
    lastFrameTime = now;
    indexHue = indexHue + RAINBOW_SPEED;  // This is a 16-bit number which will automatically roll over to 0 after 65535
    
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        matrix.drawPixel(x, y, convertTo565(matrix.ColorHSV(indexHue + (x * hueWidthPerPanel) + panelOffset, 255, 255)));
      }
    }
    matrix.show();
  }
}

void gold() {
  unsigned long now = millis();
  if (now - lastFrameTime > 50) {
    lastFrameTime = now;
    for (uint8_t y = 0; y <= MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        matrix.drawPixel(x, y, convertTo565(matrix.ColorHSV(8192, 255, 180)));
      }
    }
    sparkle(30);
    matrix.show();
  }
}

void sparkle(uint8_t count) {
  for (uint8_t i=0; i<count; i++) {
      matrix.drawPixel(random(MATRIX_WIDTH), random(MATRIX_HEIGHT), convertTo565(matrix.ColorHSV(8192, 255, 255)));
  }
}

// void rainbowCycle(uint8_t wait) {
//   for (int i = 0; i < 255; i++) {
//     for (int j = 0; j < matrix.width(); j++) {
//       matrix.setPixelColor(j, matrix.Color(sin(i + j * 0.01) * 127 + 128, sin(i + j * 0.02) * 127 + 128, sin(i + j * 0.03) * 127 + 128));
//     }
//     matrix.show();
//     delay(wait);
//   }
// }

// checkMatrix is a utility to just test each pixel from top-left to bottom-right
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


// Function to convert 24-bit color to 16-bit 565
uint16_t convertTo565(uint32_t color24) {
  // Extract 8-bit red, green, and blue components
  uint8_t r8 = (color24 >> 16) & 0xFF;
  uint8_t g8 = (color24 >> 8) & 0xFF;
  uint8_t b8 = color24 & 0xFF;
  
  // Convert to 565 format:
  // Red:   5 bits  (r8 >> 3) shifted into bits 15-11
  // Green: 6 bits  (g8 >> 2) shifted into bits 10-5
  // Blue:  5 bits  (b8 >> 3) in bits 4-0
  uint16_t color565 = ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
  // print565Components(color565);
  return color565;
}

  // Function to extract and print the red, green, and blue components from a 565 color
void print565Components(uint16_t color565) {
  // Extract 5-bit red (bits 15-11)
  uint8_t red = (color565 >> 11) & 0x1F;
  // Extract 6-bit green (bits 10-5)
  uint8_t green = (color565 >> 5) & 0x3F;
  // Extract 5-bit blue (bits 4-0)
  uint8_t blue = color565 & 0x1F;
  
  // Print the values to the Serial Monitor in base-10
  Serial.print("Red: ");
  Serial.println(red);
  Serial.print("Green: ");
  Serial.println(green);
  Serial.print("Blue: ");
  Serial.println(blue);
}

