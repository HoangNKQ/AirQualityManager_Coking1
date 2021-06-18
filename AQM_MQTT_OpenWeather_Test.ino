//button pin for interrupt
#define button 4

//Library for BME2800 sensor and I2c
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//Library for TFT Display and SPI
#include <Adafruit_GFX.h>     //Core Graphic function library
#include <Adafruit_ST7735.h>  //Library for ST7735 TFT_LCD
#include <SPI.h>

//Library for UART and PMS7003 sensor
#include <HardwareSerial.h>
#include "PMS.h"

//Library for Wifi and MQTT publish/subcribe
#include <WiFi.h>
#include <PubSubClient.h>

//Library for HTTP request and parsing Json
#include <HTTPClient.h>
#include <Arduino_JSON.h>

//Pins used for UART2 of ESP32
#define RXD  16//25  
#define TXD  17//26

//Define CS(Chip Select), RST(Reset), DC(Data command) pin from TFT_LCD to ESP32
//Define RST as -1 if TFT_LCD and ESP32's Reset pins are connect together
#define TFT_CS      32 //4
#define TFT_RST     27 //2
#define TFT_DC      33 //0

//Define mqtt topics that esp32 publish to
#define mqtt_temp_pub "Coking/Project1/AQM/temp"
#define mqtt_humid_pub "Coking/Project1/AQM/humid"
#define mqtt_hPa_pub "Coking/Project1/AQM/hPa"
#define mqtt_pm1_0_pub "Coking/Project1/AQM/pm1_0"
#define mqtt_pm2_5_pub "Coking/Project1/AQM/pm2_5"
#define mqtt_pm10_0_pub "Coking/Project1/AQM/pm10_0"

//For 1.44" screen TFT
Adafruit_ST7735 tft =  Adafruit_ST7735 (TFT_CS, TFT_DC, TFT_RST);

//For BME280
Adafruit_BME280 bme;

//Initialize UART1
HardwareSerial pmsSerial(2);

//Initialize PMS
PMS pms(pmsSerial);
PMS::DATA data;

//Wifi Client and MQTT Client
WiFiClient espClient;
PubSubClient client(espClient);


//Wifi name and password
const char* ssid = "";
const char* password = "";

//MQTT port and server 
const uint16_t mqtt_port = 1883;
const char* mqtt_server = "test.mosquitto.org";

//Variable for MQTT publish timing 
unsigned long int lastMqttMsg = 0;
const int delayMqttMsg = 5000;

//Variables to store sensors' values
volatile unsigned int pm1_0 = 0;
volatile unsigned int pm2_5 = 0;
volatile unsigned int pm10_0 = 0;
volatile int temp = 0;
volatile int humid = 0;
volatile int hPa = 0;
volatile int tempPublic = 0;
volatile int humidPublic = 0;
volatile int hPaPublic = 0;

//Variables for timimg of Display Switching
volatile uint8_t displayCount = 0;
unsigned long int preTimeDisplay = 0;
const int delayDisplay = 2000;

//Variables for Interrupt Sevice Routine
volatile uint8_t mode = 0;
unsigned long debounceTimer = 0;
const int debounce = 250;

// API key and City and URL path
String openWeatherMapApiKey = "a31f16d9f2e107673ba54e42e680ee46";
String city = "Hanoi";
String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&units=metric&APPID=" + openWeatherMapApiKey;

//Variable for API call timing
unsigned long lastAPICall = 0;
unsigned long delayAPI = 10000; //10s for testing

//String to store json
String jsonBuffer;


void setup() {
  pinMode(button, INPUT_PULLUP);
  
  Serial.begin(9600);
  pmsSerial.begin(9600, SERIAL_8N1, RXD, TXD); //Start UART1 

  unsigned status = bme.begin(0x76);
  if (!status){
    Serial.println ("Sensor not available!");
  }
  
  attachInterrupt(digitalPinToInterrupt(button), changeDisplay, RISING);

  tft.initR(INITR_144GREENTAB);
  tft.fillScreen(ST77XX_BLACK);
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  if (pms.read(data))
  {
    pm1_0 = data.PM_AE_UG_1_0;
    pm2_5 = data.PM_AE_UG_2_5;
    pm10_0 = data.PM_AE_UG_10_0;
  }
  temp = bme.readTemperature();
  humid = bme.readHumidity();
  hPa = bme.readPressure() / 100.0F;
  
  getOpenWeatherMapData();
  
  displayContent();
  publishMQTT();
}
//Interrupt Service Routine for button.
void changeDisplay()
{
  if (millis() - debounceTimer >= debounce)
  {
    mode = mode + 1;
    if (mode == 3)
    {
      mode = 0;
    }
    debounceTimer = millis();
  }
}

void getOpenWeatherMapData()
{
   // Send an HTTP GET request
  if ((millis() - lastAPICall) > delayAPI) {
    // Check WiFi connection status
    if(WiFi.status()== WL_CONNECTED){ 
           
      jsonBuffer = httpGETRequest(serverPath.c_str());

      Serial.println(jsonBuffer);

      JSONVar myObject = JSON.parse(jsonBuffer);

      Serial.println (JSON.typeof(myObject));
      
      // JSON.typeof(jsonVar) can be used to get the type of the var
      if (JSON.typeof(myObject) == "undefined") {
        Serial.println("Parsing input failed!");
        return;
      }
      
      tempPublic = myObject["main"]["temp"];
      humidPublic = myObject["main"]["humidity"];
      hPaPublic = myObject["main"]["pressure"];
//      Serial.print("JSON object = ");
//      Serial.println(myObject);
    }
    else {
      Serial.println("WiFi Disconnected");
    }
    lastAPICall = millis();
  }
}

String httpGETRequest(const char* serverName) {
  HTTPClient http;
    
  // Your IP address with path or Domain name with URL path 
  http.begin(serverName);
  
  // Send HTTP GET request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

//Display Sensors' data on lcd with different modes.
void displayContent()
{
  switch (mode){
    case 0:
      if (millis() - preTimeDisplay >= delayDisplay) {
        displayCount = displayCount + 1;
        if (displayCount == 4)
        {
          displayCount = 0;
        }
        if (displayCount == 0){
          tft.fillScreen(ST77XX_BLACK);
          displayTemp();
        }
        if (displayCount == 1){
          tft.fillScreen(ST77XX_BLACK);
          displayHumid();
        }
        if (displayCount == 2){
          tft.fillScreen(ST77XX_BLACK);
          displayhPa();
        }
        if (displayCount == 3){
          tft.fillScreen(ST77XX_BLACK);
          displayPMS();
        }
        preTimeDisplay = millis();
      }
      break;
      
    case 1:
      displayPMS();
      break;

    case 2:
      displayOpenWeatherMap();
      break;
  }
}

//Publish MQTT message to chosen Topics
void publishMQTT()
{
  if (millis() - lastMqttMsg >= delayMqttMsg)
  {
    char tempString[8];
    dtostrf(temp, 2, 0, tempString);
    char humidString[8];
    dtostrf(humid, 2, 0, humidString);
    char hPaString[8];
    dtostrf(hPa, 2, 0, hPaString);
    char pm1_0String[8];
    dtostrf(pm1_0, 2, 0, pm1_0String);
    char pm2_5String[8];
    dtostrf(pm2_5, 2, 0, pm2_5String);
    char pm10_0String[8];
    dtostrf(pm10_0, 2, 0, pm10_0String);

    client.publish(mqtt_temp_pub, tempString);
    client.publish(mqtt_humid_pub, humidString);
    client.publish(mqtt_hPa_pub, hPaString);
    client.publish(mqtt_pm1_0_pub, pm1_0String);
    client.publish(mqtt_pm2_5_pub, pm2_5String);
    client.publish(mqtt_pm10_0_pub, pm10_0String);
    
    lastMqttMsg = millis();
  }
}

//Check for WIFI connection in setup()
void setup_wifi() 
{
  delay(10);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(0,20);
  tft.println("CONNECTING TO ");
  tft.println("");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println(ssid);
//  Serial.println();
//  Serial.print("Connecting to ");
//  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
    tft.print(".");
  }
  tft.println("\n");
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.println("WIFI CONNECTED!");
//  Serial.println("");
//  Serial.println("WiFi connected");
//  Serial.println("IP address: ");
//  Serial.println(WiFi.localIP());
}

//Check connection to MQTT server
void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32_clientID")) 
    {
      Serial.println("connected");
      // Subscribe
      //client.subscribe("hehehe/testesp32");
    } else 
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


/*
 * TFT DISPLAY FUNCTIONS
 * (Under Construction!!!)
 */
void displayTemp()
{
  tft.setTextWrap(false);

  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 20);
  tft.println("TEMPERATURE(C)");

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 30);
  tft.println(temp);
}

void displayHumid()
{
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 20);
  tft.println("HUMIDITY(%)");

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 30);
  tft.println(humid);
}

void displayhPa()
{
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 20);
  tft.println("PRESSURE(hPa)");

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 30);
  tft.println(hPa);
}

void displayPMS()
{
  tft.setTextWrap(false);
  tft.setCursor(0,20);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.print("PM 1.0: ");
  tft.setCursor(0,30);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(pm1_0);

  tft.setCursor(0,55);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.print("PM 2.5: ");
  tft.setCursor(0,65);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(pm2_5);

  tft.setCursor(0,90);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.print("PM 10.0: ");
  tft.setCursor(0,100);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(pm10_0);
}

void displayBME()
{
  tft.setTextWrap(false);

  tft.setCursor(0,20);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("TEMPERATURE(C)");
  tft.setCursor(0,30);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(temp);

  tft.setCursor(0,55);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("HUMIDITY(%)");
  tft.setCursor(0,65);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(humid);

  tft.setCursor(0,90);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("PRESSURE(hPa)");
  tft.setCursor(0,100);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(hPa);
}

void displayOpenWeatherMap()
{
  tft.setTextWrap(false);
  
  tft.setCursor(1,2);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("Open Weather Map");
  
  tft.setCursor(0,20);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("TEMPERATURE(C)");
  tft.setCursor(0,30);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(tempPublic);

  tft.setCursor(0,55);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("HUMIDITY(%)");
  tft.setCursor(0,65);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(humidPublic);

  tft.setCursor(0,90);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.println("PRESSURE(hPa)");
  tft.setCursor(0,100);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println(hPaPublic);
}
