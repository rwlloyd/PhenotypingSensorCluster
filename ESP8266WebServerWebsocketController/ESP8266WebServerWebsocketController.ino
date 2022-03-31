/*
  ESP8266 Based Web and Websocket server to control active optical sensor triggers

  First things first, this code jumps around a lot. There are normal arduino-y bits, but also a lod of javascript
  and html that get sent from the microcontroller as an interface. Careful of your variables and confusion.

  ESP32 connects to a router, runs webserver and websocket server and controls the TTL outputs to the sensors.

  Webserver servers a webpage as a user interface.

  R. Lloyd. March 2022. CDT/LCAS/LAR.

  /*  ============================================================================================================

    Sample code for create a websocket on an ESP8266 board. https://youtu.be/fREqfdCphRA MrDIY.ca
    Other inspiration, taken from https://github.com/mo-thunderz/Esp32WifiPart2
    It also uses the esp8266 wifi manager library... soon... thought id best push first:
    https://randomnerdtutorials.com/wifimanager-with-esp8266-autoconnect-custom-parameter-and-manage-your-ssid-and-password/
  ============================================================================================================== */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <Hash.h>
#include <ArduinoJson.h>

String old_value, value;

ESP8266WiFiMulti    WiFiMulti;
ESP8266WebServer    server(80);
WebSocketsServer    webSocket = WebSocketsServer(81);

#define HYP_PIN 13
#define PHX_PIN 12
#define AUX_PIN 14
#define REX_PIN 15

// Set Hyperspectral Trigger GPIO
const int hypPin = HYP_PIN;
bool hypPinState = false;
int req_hyp; 
// Set Phenospex Relay GPIO
const int phxPin = PHX_PIN;
int phxPinState = false;
int req_phx; 
// Set Auxiliary Relay GPIO
const int auxPin = AUX_PIN;
int auxPinState = false;
int req_aux; 
// Set Rededge Trigger GPIO
const int rexPin = REX_PIN;
int rexPinState = false;
int req_rex; 
bool rex_done = false;

unsigned long previousMillis_hyp = 0;
unsigned long previousMillis_phx = 0;
unsigned long previousMillis_aux = 0;
unsigned long previousMillis_rex = 0;
unsigned long previousMillis_ws = 0;
unsigned long previousMillis_fb = 0;
unsigned long digital_interval = 500;
unsigned long relay_interval = 500;
unsigned long ws_interval = 1000;
unsigned long fb_dwell = 5000;

// Json stuff
StaticJsonDocument<200> doc_tx;
StaticJsonDocument<200> doc_rx;

char html_template[] PROGMEM = R"=====(
<html lang="en">
   <head>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>Phenotyping Sensor UI</title>
   </head>
   <body style="max-width:400px;margin: auto;font-family:Arial, Helvetica, sans-serif;text-align:center; padding-top:50px">
      <div><h1><br />Sensor Capture</h1></div>
      <div> The first random number is: <span id='rand1'>-</span></div>
      <div> The second random number is: <span id='rand2'>-</span></div>
      <div><button type='button' id='BTN_CAP_HYP' style="padding: 10px 10px; width:200px;">Capture HSC-2</button><span id='hyp_captured'>-</span></div>
      <div><button type='button' id='BTN_CAP_PHX' style="padding: 10px 10px; width:200px;">Capture Phenospex</button><span id='phx_captured'>-</span></div>
      <div><button type='button' id='BTN_CAP_AUX' style="padding: 10px 10px; width:200px;">Capture AUX</button><span id='aux_captured'>-</span></div>
      <div><button type='button' id='BTN_CAP_REX' style="padding: 10px 10px; width:200px;">Capture RedEdge</button><span id='rededge_captured'>-</span></div>
      <!-- <div><p id="mrdiy_value" style="font-size:100px;margin:0"></p></div> -->
      <!-- <div><button onclick="request_hyp()">Capture HSC-2</button></div> -->
      <!-- <div><button onclick="request_phx()">Capture Phenospex</button></div> -->
      
   </body>
   <script>
        // Javascript section of the webpage that will be sent to the client
        // create a variable for the websocket
        var socket;
        var msg = {
          "type": "request",
          "hyp": 0,
          "phx": 0,
          "aux": 0,
          "rex": 0
        };
        // listen for a client clicking a button
        document.getElementById('BTN_CAP_HYP').addEventListener('click', request_hyp); 
        document.getElementById('BTN_CAP_PHX').addEventListener('click', request_phx);
        document.getElementById('BTN_CAP_AUX').addEventListener('click', request_aux);
        document.getElementById('BTN_CAP_REX').addEventListener('click', request_rex);

        // init function. start websocket connection.
        function init(){
          
          socket = new WebSocket("ws:/" + "/" + location.host + ":81");
          socket.onopen = function(e) {console.log("[socket] socket.onopen "); };
          socket.onerror = function(e) {  console.log("[socket] socket.onerror "); };
          socket.onmessage = function(event) {  
            processCommand(event);
          };
        }

        // Function to process commands recieved by the client from the ucontroller
        function processCommand(event){
          var obj = JSON.parse(event.data);
          document.getElementById('rand1').innerHTML = obj.rand1;
          document.getElementById('rand2').innerHTML = obj.rand2;
          document.getElementById('rededge_captured').innerHTML = obj.rededge_captured;
          // &#10003; // green tick icon
          //console.log(obj.rand1);
          //console.log(obj.rand2);
          //console.log("[socket] " + event.data);
          //document.getElementById("my_value").innerHTML = obj.value;
        }

        // Functions to send data from the client to the microcontroller after button presses
        function request_hyp(){
          console.log("Capturing HyperSpectral");
          msg.hyp = 1;
          socket.send(JSON.stringify(msg));
          msg.hyp = 0;
        }
        function request_phx(){
          console.log("Capturing Phenospex");
          msg.phx = 1;
          socket.send(JSON.stringify(msg));
          msg.phx = 0;
        }
        function request_aux(){
          console.log("Triggering Auxiliary");
          msg.aux = 1;
          socket.send(JSON.stringify(msg));
          msg.aux = 0;
        }
        function request_rex(){
          console.log("Triggering RedEdge");
          msg.rex = 1;
          socket.send(JSON.stringify(msg));
          msg.rex = 0;
        }

        window.onload = function(event){
          init();
        }
      </script>
</html>
)=====";


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;

    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        // send message to client
        webSocket.sendTXT(num, "0");
      }
      break;

    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);
       // try to decipher the JSON string received
      DeserializationError error = deserializeJson(doc_rx, payload);
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      } else {
        // JSON string was received correctly, so information can be retrieved:
        const char* msg_type = doc_rx["type"];
        req_hyp = doc_rx["hyp"];
        req_phx = doc_rx["phx"];
        req_aux = doc_rx["aux"];
        req_rex = doc_rx["rex"];

        //      uint8_t command = (uint8_t) strtol((const char *) &payload[0], NULL, 16);
       if(String(msg_type) == "request"){
         Serial.printf("Got Request hyp:%d, phx:%d, aux:%d, rex:%d\n", req_hyp, req_phx, req_aux, req_rex);
//         if (req_hyp == 1){
//            webSocket.sendTXT(num, "HyperSpectral Triggered");
//          } else if (req_phx == 1){
//            webSocket.sendTXT(num, "Phenospex Triggered");
//          } else if (req_aux == 1){
//            webSocket.sendTXT(num, "Auxiliary Triggered");
//          }
        }
      }
      
      // This next line is copied from the last time i did something like this...

      // send message to client
      // webSocket.sendTXT(num, "message here");
      // send data to all connected clients
      // webSocket.broadcastTXT("message here");
      break;
      
//    case WStype_BIN:
//      Serial.printf("[%u] get binary length: %u\n", num, length);
////      hexdump(payload, length);
//      // send message to client
//      // webSocket.sendBIN(num, payload, length);
//      break;
  }

}

void setup() {

  pinMode(hypPin, OUTPUT);
  pinMode(phxPin, OUTPUT);
  pinMode(auxPin, OUTPUT);
  pinMode(rexPin, OUTPUT);

  digitalWrite(hypPin, LOW);
  digitalWrite(phxPin, LOW); 
  digitalWrite(auxPin, LOW); 
  digitalWrite(rexPin, LOW);
  
  Serial.begin(115200);
  delay(100);

  WiFiMulti.addAP("PhenoNet", "Name1234");
  //WiFiMulti.addAP("oort", "heliopause");

  while (WiFiMulti.run() != WL_CONNECTED) {
      Serial.println("Connecting...");
      delay(100);
  }

  Serial.println(WiFi.localIP());

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  server.on("/", handleMain);
  server.onNotFound(handleNotFound);
  server.begin();

}

void handleMain() {
  server.send_P(200, "text/html", html_template ); 
}
void handleNotFound() {
  server.send(404,   "text/html", "<html><body><p>404 Error</p></body></html>" );
}

void loop() { 
  webSocket.loop();
  server.handleClient();
  
  // STATE MACHINE FOR CONTROLLING TTL OUTPUTS TO SENSORS
  unsigned long currentMillis = millis();
  // Only spaff random numbers to the websocket at a normal interval
  if (currentMillis - previousMillis_ws >= ws_interval){
    previousMillis_ws = currentMillis;
    // Make and send a json object
    String jsonString="";
    JsonObject object = doc_tx.to<JsonObject>();
    object["rand1"] = random(1000);
    object["rand2"] = random(1000);
    if (rex_done){
      previousMillis_fb = currentMillis;
      object["rededge_captured"] = "&#10003;";
    } 
    if ((rex_done) && (currentMillis - previousMillis_fb >= fb_dwell)){
      previousMillis_fb = currentMillis;
    } else {
      object["rededge_captured"] = "-";
    }
    
    //object["hsc"] = hypPinState;
    //object["phx"] = phxPinState;
    //object["aux"] = auxPinState;
    serializeJson(doc_tx, jsonString);
    //Serial.println(jsonString);
    webSocket.broadcastTXT(jsonString);
  }

//  Triggerd the Hyepspectral
  if (req_hyp == 1) {
    Serial.println("Hyperspectral Capture Requested");
    hypPinState = !hypPinState;
    previousMillis_hyp = currentMillis;
    req_hyp = 0;
  } 
  if ((hypPinState) && (currentMillis - previousMillis_hyp >= digital_interval)){
      hypPinState = !hypPinState;
      Serial.println("HyperSpectral triggered");
      previousMillis_hyp = currentMillis;
    } 
  digitalWrite(hypPin, hypPinState);

// Trigger the Phenospex
    if (req_phx == 1) {
    Serial.println("Phenospex Capture Requested");
    phxPinState = !phxPinState;
    previousMillis_phx = currentMillis;
    req_phx = 0;
  } 
  if ((phxPinState) && (currentMillis - previousMillis_phx >= digital_interval)){
      phxPinState = !phxPinState;
      Serial.println("Phenospex triggered");
      previousMillis_phx = currentMillis;
    } 
  digitalWrite(phxPin, phxPinState);

// Trigger the Auxiliary Relay
    if (req_aux == 1) {
    Serial.println("Auxiliary Relay Requested");
    auxPinState = !auxPinState;
    previousMillis_aux = currentMillis;
    req_aux = 0;
  } 
  if ((auxPinState) && (currentMillis - previousMillis_aux >= digital_interval)){
      auxPinState = !auxPinState;
      Serial.println("Auxiliary Relay triggered");
      previousMillis_aux = currentMillis;
    } 
  digitalWrite(auxPin, auxPinState);

  // Trigger the Rededge Camera
    if (req_rex == 1) {
    Serial.println("Rededge Capture Requested");
    rexPinState = !rexPinState;
    previousMillis_rex = currentMillis;
    req_rex = 0;
  } 
  if ((rexPinState) && (currentMillis - previousMillis_rex >= digital_interval)){
      rexPinState = !rexPinState;
      Serial.println("Rededge Capture triggered");
      rex_done = true;
      previousMillis_rex = currentMillis;
    } 
  digitalWrite(rexPin, rexPinState);

}
