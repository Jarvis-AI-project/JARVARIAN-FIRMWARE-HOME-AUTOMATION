#include <Arduino.h>
#include <PubSubClient.h>

// Include Functionality according to the board
#if !(defined(ESP32) || defined(ESP8266))
#error "This code is intended to run on the ESP32 or ESP8266 platform!"
#endif

#ifdef ESP32
int is_esp32 = 1;
int is_esp8266 = 0;

#include <WiFi.h>
#include <WebServer.h>
WebServer server(80);

// can use both at same pins.
#define STATUS_BUZZER 27
#define BUILTIN_LED 2
#endif

#ifdef ESP8266
int is_esp32 = 0;
int is_esp8266 = 1;

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
ESP8266WebServer server(80);

#define STATUS_BUZZER D0
#define BUILTIN_LED D4
#endif

#ifdef DEBUG
int debug = 1;
#else
int debug = 0;
#endif

// Load config file
#include <loadConfig.h>
DynamicJsonDocument configDoc(JSON_FILE_SIZE);

// MQTT Client
WiFiClient espClient;
PubSubClient mqttclient(espClient);
// Setting `0` as first `keep-alive` message is sent immediately after connection.
unsigned long previousMillis = 0;

void connectToWiFi(JsonDocumentType &configDoc)
{
  // Set static IP
  String static_ip = configDoc["wifi"]["ip"].as<String>();
  String gateway = configDoc["wifi"]["gateway"].as<String>();
  String subnet = configDoc["wifi"]["subnet"].as<String>();
  String dns = configDoc["wifi"]["dns"].as<String>();

  IPAddress ip;
  IPAddress gateway_ip;
  IPAddress subnet_ip;
  IPAddress dns_ip;
  ip.fromString(static_ip);
  gateway_ip.fromString(gateway);
  subnet_ip.fromString(subnet);
  dns_ip.fromString(dns);

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Setting static IP...");
    Serial.print("IP: ");
    Serial.println(static_ip);
    Serial.print("Gateway: ");
    Serial.println(gateway);
    Serial.print("Subnet: ");
    Serial.println(subnet);
    Serial.print("DNS: ");
    Serial.println(dns);
  }
  WiFi.config(ip, gateway_ip, subnet_ip, dns_ip);

  // Set hostname
  const char *hostname = configDoc["wifi"]["hostname"].as<const char *>();
  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Setting hostname...");
    Serial.println("Hostname: " + String(hostname));
  }
  WiFi.setHostname(hostname);

  // Connect to WiFi network
  String ssid = configDoc["wifi"]["ssid"].as<String>();
  String password = configDoc["wifi"]["password"].as<String>();

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Setting up WiFi Credentials...");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);
  }

  WiFi.begin(ssid, password);

  // Wait for connection
  if (debug)
    Serial.println("-----------------------");
  while (WiFi.status() != WL_CONNECTED)
  {
    statusBuzzer(2, 100);
    if (debug)
      Serial.println("Connecting to WiFi...");
    delay(2000); // 2s delay
  }

  if (debug)
  {
    Serial.println("Connected to the WiFi network");
    Serial.println(String(WiFi.getHostname()) + " @ " + WiFi.localIP().toString());
  }
}

void connectToMQTT(JsonDocumentType &configDoc)
{
  String clientID = configDoc["mqtt"]["clientID"].as<String>();
  String username = configDoc["mqtt"]["username"].as<String>();
  String password = configDoc["mqtt"]["password"].as<String>();
  String topic = configDoc["mqtt"]["topic"].as<String>();

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Connecting to MQTT Broker...");
    Serial.print("Client ID: ");
    Serial.println(clientID);
    Serial.print("Username: ");
    Serial.println(username);
    Serial.print("Password: ");
    Serial.println(password);
  }

  if (mqttclient.connect(clientID.c_str(), username.c_str(), password.c_str()))
  {
    if (debug)
    {
      Serial.println("Connected to MQTT Broker");
      Serial.println("Subscribing to topic: " + topic);
    }
    mqttclient.subscribe(topic.c_str());
  }
  else
  {
    if (debug)
    {
      Serial.println("MQTT connection failed");
      Serial.print("Error code: ");
      Serial.println(mqttclient.state());
    }
    statusBuzzer(5, 100);
  }
}

void handleRootGet()
{
  server.send(200, "text/plain", "Hello from " + String(WiFi.getHostname()) + " @ " + WiFi.localIP().toString());
  statusBuzzer(1, 100);
}

void handleConfigGet()
{
  String configString;
  serializeJson(configDoc, configString);

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Sending config file");
    Serial.println(configString);
  }

  server.send(200, "application/json", configString);

  // Buzzer status
  statusBuzzer(1, 100);
}

void methodNotAllowed()
{
  server.send(405, "text/plain", "405: Method Not Alloweed");
  statusBuzzer(1, 100);
}

void notFound()
{
  server.send(404, "text/plain", "404: Not found");
  statusBuzzer(1, 100);
}

void callback(char *topic, byte *payload, unsigned short int length)
{
  String server = configDoc["mqtt"]["host"].as<String>();
  String topicString = configDoc["mqtt"]["topic"].as<String>();

  // Convert payload to a string
  String message;
  for (unsigned short int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  if (message.equals("keep-alive"))
    return; // Skip processing for keep-alive messages
  if (message.equals("status"))
    return; // Skip processing for keep-alive messages
  if (message.equals("OK"))
    return; // Skip processing for keep-alive messages
  if (message.equals("Invalid state"))
    return; // Skip processing for ERROR messages
  if (message.equals("Not a valid JSON message"))
    return; // Skip processing for ERROR messages

  if (message.equals("status"))
  {
    mqttclient.publish(topicString.c_str(), "OK");
    return;
  }

  // if debug is enabled, print the message
  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Message arrived in topic: " + String(topic));
    Serial.println("Message: " + String(message));
  }

  // Handle the message if it's a JSON
  DynamicJsonDocument doc(64);
  DeserializationError error = deserializeJson(doc, message);
  if (error)
  {
    Serial.println("Not a valid JSON message");
    mqttclient.publish(topic, "Not a valid JSON message");
    return;
  }

  if (doc.containsKey("pin") && doc.containsKey("state"))
  {
    int pin = doc["pin"].as<int>();
    const String state = doc["state"];

    if (state.equals("ON"))
    {
      digitalWrite(pin, HIGH);
      mqttclient.publish(topic, "OK");
      if (debug)
      {
        Serial.println("Changing pin state: ");
        Serial.println("Pin: " + String(pin));
        Serial.println("State: " + state);
      }
    }
    else if (state.equals("OFF"))
    {
      digitalWrite(pin, LOW);
      mqttclient.publish(topic, "OK");
      if (debug)
      {
        Serial.println("Changing pin state: ");
        Serial.println("Pin: " + String(pin));
        Serial.println("State: " + state);
      }
    }
    else
    {
      Serial.println("Invalid state");
      mqttclient.publish(topic, "Invalid state");
    }
  }
  else
  {
    Serial.println("Not a valid JSON message");
    mqttclient.publish(topic, "Not a valid JSON message");
  }
  statusBuzzer(1, 100);
}

void setup()
{
  // Initialize Serial
  Serial.begin(115200);
  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Serial initialized");
  }

  // Initialize Buzzer and In-Built LED
  pinMode(STATUS_BUZZER, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);

  // Turn ON the built-in LED
  if (is_esp32)
    digitalWrite(BUILTIN_LED, HIGH);
  else if (is_esp8266)
    // (In-Built LED works in Inverted Mode)
    digitalWrite(BUILTIN_LED, LOW);

  // Delay after power on to allow for serial monitor to be connected and to make sure beeps are heard clearly.
  delay(3000);

  // Load config file
  char jsonBuffer[JSON_FILE_SIZE];
  bool configLoaded = loadConfig(jsonBuffer);

  while (!configLoaded)
  {
    if (debug)
    {
      Serial.println("-----------------------");
      Serial.println("Failed to load config file");
    }
    statusBuzzer(3, 100);
    delay(2000);
  }

  // Successfully loaded the config file
  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Config file loaded successfully:");
  }

  // Parse the JSON document if needed
  DeserializationError error = deserializeJson(configDoc, jsonBuffer);

  while (error)
  {
    if (debug)
    {
      Serial.println("-----------------------");
      Serial.println("Failed to parse config file. ERROR: ");
      Serial.println(error.c_str());
    }
    statusBuzzer(3, 100);
    delay(2000);
  }

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Parsed JSON successfully");
  }

  // Print the config file
  if (debug)
    printConfig(configDoc);

  // Initialize pins
  initilizedPins(configDoc);

  // Connect to WiFi
  connectToWiFi(configDoc);

  // Define HTTP endpoint
  // Handle Root(/) endpoint
  server.on("/", HTTP_GET, handleRootGet);
  server.on("/", HTTP_POST, methodNotAllowed);

  // Handle Config(/config) endpoint
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, methodNotAllowed);

  // Handle 404
  server.onNotFound(notFound);

  // Start server
  server.begin();
  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("HTTP server started");
  }

  // Setup connection to MQTT Broker (Server)
  String server = configDoc["mqtt"]["host"].as<String>();
  String port = configDoc["mqtt"]["port"].as<String>();

  if (debug)
  {
    Serial.println("-----------------------");
    Serial.println("Initializing MQTT Client...");
    Serial.print("Server: ");
    Serial.println(server);
    Serial.print("Port: ");
    Serial.println(port);
  }

  mqttclient.setServer(server.c_str(), port.toInt());
  mqttclient.setCallback(callback);
  connectToMQTT(configDoc);
}

void loop()
{
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED)
  {
    if (debug)
      Serial.println("WiFi connection lost.. Reconnecting..");
    connectToWiFi(configDoc);
  }

  // publish a message roughly every 5 seconds if connected else reconnect
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > 10000)
  {
    previousMillis = currentMillis;
    if (mqttclient.connected())
      mqttclient.publish(configDoc["mqtt"]["topic"].as<String>().c_str(), "keep-alive");
    else
    {
      connectToMQTT(configDoc);
      Serial.println("-----------------------");
      Serial.println("Connection lost.. Reconnecting..");
    }
  }
  mqttclient.loop();
}