#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ESP8266mDNS.h>
#include <Event.h>
#include <Timer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include "FS.h"

//Declare Variables for Pushbullet
const char* HOST = "api.pushbullet.com";
const int HTTPSPORT = 443;
const char* FINGERPRINT = "28:92:D7:05:86:1B:9A:0A:96:A2:50:B9:08:50:70:7E:83:26:B4:F8";

//Instantiate classes
Timer timer;
ESP8266WebServer server ( 80 );
HTTPClient http;
MDNSResponder mdns;
WiFiClientSecure client;

//Setup DS18B20 Temprerature Sensor
const int ONE_WIRE_BUS = 12;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

//Setup variables for WiFi Config read from config file, dont know why they need to be pointers
const char* SSID;
const char* PSW;
const char* PushBulletAPIKey;
String apiKeyValue;

void handleRoot() {
    char temp[400];
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;
    snprintf ( temp, 400,
      "<html>\
        <head>\
          <meta http-equiv='refresh' content='5'/>\
          <title>ESP8266 Demo</title>\
          <style>\
            body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
          </style>\
        </head>\
        <body>\
          <h1>Hello from ESP8266!</h1>\
          <p>Uptime: %02d:%02d:%02d</p>\
        </body>\
      </html>",
      hr, min % 60, sec % 60
    );
    server.send ( 200, "text/html", temp );
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}

//Method to load the information from the config.json file uploaded to the board
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<1024> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  SSID = json["SSID"];
  PSW = json["PSW"];
  PushBulletAPIKey = json["PushBulletAPIKey"];
  return true;
}

void setup() {  
  //Start the serial monitor
  Serial.begin(115200);
  Serial.println("Ready");

  //Mount the file system
  if(!SPIFFS.begin()){
    Serial.println("Failed to mount file system.");
    return;  
  }

  if(!loadConfig()){
    Serial.println("Failed to load config");  
  }else{
    Serial.println("Config Loaded");  
  }
  //Setup this variable as a global variable. I tried to use PushBulletAPIKey in the function sendMsgToDevice(). But nothing came through.
  apiKeyValue = PushBulletAPIKey;
  
  //Connect to WiFi
  WiFi.begin(SSID, PSW);

  //Wait for WiFi to Connect
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
 
  //Display connection info
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //Start the mdns
  if (mdns.begin("TemperatureLogger", WiFi.localIP())){
    Serial.println("MDNS Responder Started");  
  }

  //Send message via Pushbullet
  String LocalIP = WiFi.localIP().toString();
  sendMsgToDevice(LocalIP);
  
  //Handle connection to the server (rooting)
  server.on("/", handleRoot);
  server.on("/getCurrTemperature", getCurrTemperature);
  server.onNotFound(handleNotFound);

  //Start the server
  server.begin();

  //Add MDNS Service
  MDNS.addService("http","tcp",80);

  //Setup the timer, every 10 Minutes
  timer.every(600000,logTimer_onTick);

  //Setup the DS18B20 Temperature sensor
  sensors.begin();
}

void loop() {
  server.handleClient();
  timer.update();
  yield(); 
}

void getCurrTemperature(){
  showMsg(String(getTemperature()));
}

double getTemperature(){
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

//Http Request to save the data to the cache database
void logTimer_onTick(){
    String errormsg = "";
  
    http.begin("http://192.168.101.199:57772/csp/temperaturelogger/rest/logTemperature");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST("data={\"TemperatureReading\":\""+String(getTemperature())+"\"}");
    String payload = http.getString();
    showMsg(payload);
    if(httpCode > 0) {
      errormsg = "[HTTP] POST... code: %d\n" + httpCode;
      showMsg(errormsg);
      if(httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          showMsg(payload);
      }
    }else{
      errormsg = "[HTTP] POST... failed, error: %s\n" + http.errorToString(httpCode);
      showMsg(errormsg);
      sendMsgToDevice("Could not connect to server");
    }
    http.end();
}

//Print the message to the screen
void showMsg(String prmMsg){
    String message = prmMsg;
    message += "\n";
  
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
    }
  
    server.send (200, "text/plain", message );
}

//Send message to PushBullit API
void sendMsgToDevice(String prmMsg){
  //Connect to Pushbullet
  if (!client.connect(HOST, HTTPSPORT)){
    Serial.println("Connection Failed");  
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"ESP8266\", \"body\": \"";
  messagebody = messagebody + prmMsg;
  messagebody = messagebody + "\"}\r\n";
  Serial.println(messagebody);

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
    "Host: " + HOST + "\r\n" +
    "Authorization: Bearer " + apiKeyValue + "\r\n" +
    "Content-Type: application/json\r\n" +
    "Content-Length: " +
    String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);

  //print the response
    while (client.available() == 0);

    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
      showMsg(line);
    }
 }
