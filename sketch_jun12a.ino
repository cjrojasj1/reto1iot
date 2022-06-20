#include <ESP8266WiFi.h>

#include <WiFiClientSecure.h>

#include <time.h>

#include <PubSubClient.h>

#include "DHT.h"

#define DHTTYPE DHT11
#define dht_dpin 4
#define lightsensor A0
#define DELAY1 2000
#define DELAY2 1000
#define DELAY3 500
#define DELAY4 10
#define VIN 3.3
#define R 1000
#define MAX_VOLTAGE 1023
#define SERIAL_RATE 115200
#define HOSTNAME "cj.rojasj1"

DHT dht(dht_dpin, DHTTYPE);

//Nombre de la red Wifi
const char ssid[] = "Maverick";
//Contraseña de la red Wifi
const char pass[] = "50A5DC23B3A0";
//Conexión a Mosquitto
const char MQTT_HOST[] = "iotlab.virtual.uniandes.edu.co";
const int MQTT_PORT = 8082;
//Usuario uniandes sin @uniandes.edu.co
const char MQTT_USER[] = "cj.rojasj1";
//Contraseña de MQTT que recibió por correo
const char MQTT_PASS[] = "202116099";
const char MQTT_SUB_TOPIC[] = HOSTNAME "/";
//Tópico al que se enviarán los datos de humedad
const char MQTT_PUB_TOPIC1[] = "humedad/bogota/" HOSTNAME;
//Tópico al que se enviarán los datos de temperatura
const char MQTT_PUB_TOPIC2[] = "temperatura/bogota/" HOSTNAME;
//Tópico al que se enviarán los datos de luz
const char MQTT_PUB_TOPIC3[] = "luminosidad/bogota/" HOSTNAME;

//////////////////////////////////////////////////////

#if(defined(CHECK_PUB_KEY) and defined(CHECK_CA_ROOT)) or(defined(CHECK_PUB_KEY) and defined(CHECK_FINGERPRINT)) or(defined(CHECK_FINGERPRINT) and defined(CHECK_CA_ROOT)) or(defined(CHECK_PUB_KEY) and defined(CHECK_CA_ROOT) and defined(CHECK_FINGERPRINT))
#error "cant have both CHECK_CA_ROOT and CHECK_PUB_KEY enabled"
#endif

BearSSL::WiFiClientSecure net;
PubSubClient client(net);

time_t now;
unsigned long lastMillis = 0;
float h;
float t;
float l;
float lux;
String json;

//Función que conecta el node a través del protocolo MQTT
//Emplea los datos de usuario y contraseña definidos en MQTT_USER y MQTT_PASS para la conexión
void mqtt_connect() {
  //Intenta realizar la conexión indefinidamente hasta que lo logre
  while (!client.connected()) {
    Serial.print("Time: ");
    Serial.print(ctime( & now));
    Serial.print("MQTT connecting ... ");
    if (client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected.");
    } else {
      Serial.println("Problema con la conexión, revise los valores de las constantes MQTT");
      Serial.print("Código de error = ");
      Serial.println(client.state());
      if (client.state() == MQTT_CONNECT_UNAUTHORIZED) {
        ESP.deepSleep(0);
      }
      /* Espera antes de volver a intentar */
      delay(DELAY1);
    }
  }
}

void receivedCallback(char * topic, byte * payload, unsigned int length) {
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }
}

//Configura la conexión del node MCU a Wifi y a Mosquitto
void setup() {
  Serial.begin(SERIAL_RATE);
  Serial.println();
  Serial.println();
  Serial.print("Attempting to connect to SSID: ");
  Serial.print(ssid);
  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  //Intenta conectarse con los valores de las constantes ssid y pass a la red Wifi
  //Si la conexión falla el node se dormirá hasta que lo resetee
  while (WiFi.status() != WL_CONNECTED) {
    if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_WRONG_PASSWORD) {
      Serial.print("\nProblema con la conexión, revise los valores de las constantes ssid y pass");
      ESP.deepSleep(0);
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.print("\nNo se ha logrado conectar con la red, resetee el node y vuelva a intentar");
      ESP.deepSleep(0);
    }
    Serial.print(".");
    delay(DELAY2);
  }
  Serial.println("connected!");

  //Sincroniza la hora del dispositivo con el servidor SNTP (Simple Network Time Protocol)
  Serial.print("Setting time using SNTP");
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < 1510592825) {
    delay(DELAY3);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r( & now, & timeinfo);
  //Una vez obtiene la hora, imprime en el monitor el tiempo actual
  Serial.print("Current time: ");
  Serial.print(asctime( & timeinfo));

  #ifdef CHECK_CA_ROOT
  BearSSL::X509List cert(digicert);
  net.setTrustAnchors( & cert);
  #endif
  #ifdef CHECK_PUB_KEY
  BearSSL::PublicKey key(pubkey);
  net.setKnownKey( & key);
  #endif
  #ifdef CHECK_FINGERPRINT
  net.setFingerprint(fp);
  #endif
  #if(!defined(CHECK_PUB_KEY) and!defined(CHECK_CA_ROOT) and!defined(CHECK_FINGERPRINT))
  net.setInsecure();
  #endif

  //Llama a funciones de la librería PubSubClient para configurar la conexión con Mosquitto
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(receivedCallback);
  //Llama a la función de este programa que realiza la conexión con Mosquitto
  mqtt_connect();
}

//Función loop que se ejecuta indefinidamente repitiendo el código a su interior
//Cada vez que se ejecuta toma nuevos datos de la lectura del sensor
void loop() {
  //Revisa que la conexión Wifi y MQTT siga activa
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Checking wifi");
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      Serial.print(".");
      delay(DELAY4);
    }
    Serial.println("connected");
  } else {
    if (!client.connected()) {
      mqtt_connect();
    } else {
      client.loop();
    }
  }

  now = time(nullptr);
  //Lee los datos del sensor
  h = dht.readHumidity();
  t = dht.readTemperature();
  l = analogRead(lightsensor);

  //Si los valores recolectados no son indefinidos, se envían al tópico correspondiente
  if (!isnan(t) && !isnan(l) && !isnan(h)) {

    lux = sensorRawToPhys(l);

    //Transforma la información a la notación JSON para poder enviar los datos 
    //El mensaje que se envía es de la forma {"value": x}, donde x es el valor de temperatura o humedad

    //JSON para humedad
    json = "{\"value\": " + String(h) + "}";
    char payload1[json.length() + 1];
    json.toCharArray(payload1, json.length() + 1);

    //JSON para temperatura
    json = "{\"value\": " + String(t) + "}";
    char payload2[json.length() + 1];
    json.toCharArray(payload2, json.length() + 1);

    //JSON para luminosidad
    json = "{\"value\": " + String(lux) + "}";
    char payload3[json.length() + 1];
    json.toCharArray(payload3, json.length() + 1);

    //Publica en el tópico de la humedad
    client.publish(MQTT_PUB_TOPIC1, payload1, false);
    //Imprime en el monitor serial la información recolectada
    Serial.print(MQTT_PUB_TOPIC1);
    Serial.print(" -> ");
    Serial.println(payload1);

    //Publica en el tópico de la temperatura
    client.publish(MQTT_PUB_TOPIC2, payload2, false);
    //Imprime en el monitor serial la información recolectada
    Serial.print(MQTT_PUB_TOPIC2);
    Serial.print(" -> ");
    Serial.println(payload2);

    //Publica en el tópico de la luminosidad
    client.publish(MQTT_PUB_TOPIC3, payload3, false);
    //Imprime en el monitor serial la información recolectada
    Serial.print(MQTT_PUB_TOPIC3);
    Serial.print(" -> ");
    Serial.println(payload3);

    Serial.println("");

  } else {
    if (isnan(t)) {
      Serial.println("Failed to read temperature from DHT sensor!");
    }

    if (isnan(h)) {
      Serial.println("Failed to read humidity from DHT sensor!");
    }

    if (isnan(l)) {
      Serial.println("Failed to read brightness from GL sensor!");
    }

  }

  /*Espera para volver a ejecutar la función loop*/
  delay(DELAY1);
}

int sensorRawToPhys(int raw) {
  // Regla de conversion
  float Vout = float(raw) * (VIN / float(MAX_VOLTAGE)); // Conversion de lectura analoga a voltaje
  float RLDR = (R * (VIN - Vout)) / Vout; // Conversion de voltaje a resistencia
  int phys = 500 / (RLDR / 1000); // Conversion de resustencia a Lux
  return phys;

}
