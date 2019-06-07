#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson (use v6.xx)
#include <time.h>
#define emptyString String()

//Follow instructions from https://github.com/debsahu/ESP-MQTT-AWS-IoT-Core/blob/master/doc/README.md
//Enter values in secrets.h ▼
#include "secrets.h"

#if !(ARDUINOJSON_VERSION_MAJOR == 6 and ARDUINOJSON_VERSION_MINOR >= 7)
#error "Install ArduinoJson v6.7.0-beta or higher"
#endif

#define USE_ARDUINO_INTERRUPTS false
#include <PulseSensorPlayground.h>

const int MQTT_PORT = 8883;
const char MQTT_SUB_TOPIC[] = THINGNAME"/in";//"$aws/things/" THINGNAME "/shadow/update/out";
const char MQTT_PUB_TOPIC[] = THINGNAME"/out";//"$aws/things/" THINGNAME "/shadow/update/out";

#ifdef USE_SUMMER_TIME_DST
uint8_t DST = 1;
#else
uint8_t DST = 0;
#endif

WiFiClientSecure net;

#ifdef ESP8266
BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);
#endif

PubSubClient client(net);

unsigned long lastMillis = 0;
time_t now;
time_t nowish = 1510592825;


const int PULSE_INPUT = A0;
const int PULSE_BLINK = 2;    // Pin 2 is the on-board LED
const int PULSE_FADE = 5;
const int THRESHOLD = 530;   // Adjust this number to avoid noise when idle
int signalPulse = 0;
int bpm = 0;
byte samplesUntilReport;
const byte SAMPLES_PER_SERIAL_SAMPLE = 10;
bool iniciarMedida = false;
int estadoActuador = 1;
const int ACTUATOR_LED = 16;

PulseSensorPlayground pulseSensor;

void NTPConnect(void)
{
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, DST * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void messageReceived(char *topic, byte *payload, unsigned int length)
{
  //Serial.print("Tamaño msg: ");
  //Serial.print(length);
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(2) + 100);
  Serial.println("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  char message[length];
  for (int i = 0; i < length; i++)
  {
    message[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println(message);
  deserializeJson(doc, message);
  JsonObject objReceived = doc.as<JsonObject>();
  if(objReceived["object"] == "context"){
    if(objReceived["action"] == "start"){
      iniciarMedida = true;
    }
    else{
      iniciarMedida = false;
    }
  }
  else{
    if(objReceived["action"] == "start"){
      estadoActuador = 0;      
    }
    else{
      estadoActuador = 1;
    }
    Serial.println("Estado LED: ");
    Serial.print(estadoActuador);
  }  
}

void pubSubErr(int8_t MQTTErr)
{
  if (MQTTErr == MQTT_CONNECTION_TIMEOUT)
    Serial.print("Connection tiemout");
  else if (MQTTErr == MQTT_CONNECTION_LOST)
    Serial.print("Connection lost");
  else if (MQTTErr == MQTT_CONNECT_FAILED)
    Serial.print("Connect failed");
  else if (MQTTErr == MQTT_DISCONNECTED)
    Serial.print("Disconnected");
  else if (MQTTErr == MQTT_CONNECTED)
    Serial.print("Connected");
  else if (MQTTErr == MQTT_CONNECT_BAD_PROTOCOL)
    Serial.print("Connect bad protocol");
  else if (MQTTErr == MQTT_CONNECT_BAD_CLIENT_ID)
    Serial.print("Connect bad Client-ID");
  else if (MQTTErr == MQTT_CONNECT_UNAVAILABLE)
    Serial.print("Connect unavailable");
  else if (MQTTErr == MQTT_CONNECT_BAD_CREDENTIALS)
    Serial.print("Connect bad credentials");
  else if (MQTTErr == MQTT_CONNECT_UNAUTHORIZED)
    Serial.print("Connect unauthorized");
}

void connectToMqtt(bool nonBlocking = false)
{
  Serial.print("MQTT connecting ");
  while (!client.connected())
  {
    Serial.println(THINGNAME);
    if (client.connect(THINGNAME))
    {
      Serial.println("connected!");
      if (!client.subscribe(MQTT_SUB_TOPIC))
        pubSubErr(client.state());
    }
    else
    {
      Serial.print("failed, reason -> ");
      pubSubErr(client.state());
      if (!nonBlocking)
      {
        Serial.println(" < try again in 5 seconds");
        delay(5000);
      }
      else
      {
        Serial.println(" <");
      }
    }
    if (nonBlocking)
      break;
  }
}

void connectToWiFi(String init_str)
{
  if (init_str != emptyString)
    Serial.print(init_str);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
  }
  if (init_str != emptyString)
    Serial.println("ok!");
}

void checkWiFiThenMQTT(void)
{
  connectToWiFi("Checking WiFi");
  connectToMqtt();
}

unsigned long previousMillis = 0;
const long interval = 5000;

void checkWiFiThenMQTTNonBlocking(void)
{
  connectToWiFi(emptyString);
  if (millis() - previousMillis >= interval && !client.connected()) {
    previousMillis = millis();
    connectToMqtt(true);
  }
}

void checkWiFiThenReboot(void)
{
  connectToWiFi("Checking WiFi");
  Serial.print("Rebooting");
  ESP.restart();
}

void sendData(void)
{
  /*DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);
  JsonObject root = jsonBuffer.to<JsonObject>();
  JsonObject state = root.createNestedObject("state");
  JsonObject state_reported = state.createNestedObject("reported");
  state_reported["value"] = random(100);*/
  DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);
  //StaticJsonDocument<256> doc;
  JsonObject root = jsonBuffer.to<JsonObject>();
  root["context_id"] = "37ed3e20\-6809\-41c5\-9c1d\-380ca2155fa8";
  JsonObject dm  =root.createNestedObject("device");
  dm["id"] = "4d075988\-07df\-444b\-bd38\-2bbf0789a607";
  dm["signal"] = pulseSensor.getLatestSample();//pulseSensor.getLatestSample();
  dm["value"] = pulseSensor.getBeatsPerMinute();
  Serial.printf("Sending  [%s]: ", MQTT_PUB_TOPIC);
  //serializeJsonPretty(root, Serial);
  //Serial.println();
  char shadow[measureJson(root) + 1];
  serializeJson(root, shadow, sizeof(shadow));
  //Serial.println("SHADOW");
  Serial.println(shadow);
  if (!client.publish(MQTT_PUB_TOPIC, shadow, false))
    pubSubErr(client.state());
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println();
  Serial.println();
#ifdef ESP32
  WiFi.setHostname(THINGNAME);
#else
  WiFi.hostname(THINGNAME);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  connectToWiFi(String("Attempting to connect to SSID: ") + String(ssid));

  NTPConnect();

#ifdef ESP32
  net.setCACert(cacert);
  net.setCertificate(client_cert);
  net.setPrivateKey(privkey);
#else
  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);
#endif

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(messageReceived);
  connectToMqtt();
  
  // Configure the PulseSensor manager.
  pulseSensor.analogInput(PULSE_INPUT);
  pulseSensor.blinkOnPulse(PULSE_BLINK);
  pulseSensor.fadeOnPulse(PULSE_FADE);  
  pulseSensor.setThreshold(THRESHOLD);
  // Skip the first SAMPLES_PER_SERIAL_SAMPLE in the loop().
  samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;
  pinMode(ACTUATOR_LED, OUTPUT);  

  // Now that everything is ready, start reading the PulseSensor signal.
  if (!pulseSensor.begin()) {
    /*
       PulseSensor initialization failed,
       likely because our Arduino platform interrupts
       aren't supported yet.

       If your Sketch hangs here, try changing USE_PS_INTERRUPT to false.
    */
    /*for(;;) {
      // Flash the led to show things didn't work.
      digitalWrite(PULSE_BLINK, LOW);
      delay(50);
      digitalWrite(PULSE_BLINK, HIGH);
      delay(50);
    }*/
  }
}

void loop()
{
  now = time(nullptr);
  if (!client.connected())
  {
    checkWiFiThenMQTT();
    //checkWiFiThenMQTTNonBlocking();
    //checkWiFiThenReboot();
  }
  else
  {
    client.loop();
    /*if (millis() - lastMillis > 500)
    {
      lastMillis = millis();
      sendData();
    }*/
    if(iniciarMedida){
      if (pulseSensor.sawNewSample()){
        /*
        Every so often, send the latest Sample.
        We don't print every sample, because our baud rate
        won't support that much I/O.
        */
        if (--samplesUntilReport == (byte) 0) {
          samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;
          sendData();
          //Serial.println(bpm);
        }
      }
    }
  }
  digitalWrite(ACTUATOR_LED, estadoActuador);
  digitalWrite(PULSE_BLINK, HIGH);
}
