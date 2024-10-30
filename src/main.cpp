#include <Arduino.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WebSocketsServer.h>
// Credenciales Wi-Fi
const char *ssid = "alejandro";
const char *password = "12345678";

// Crear servidor HTTP en el puerto 80
WebServer server(80);
const unsigned long sendInterval = 1000;
const uint16_t dataTxTimeInterval = 500; // ms
// Pin GPIO para iluminacion
int ledPin = 23;
// pin para activar sistema de riego
int riegoPin = 13;
//pin para sensores
// DHT22
const uint8_t dhtPin = 26;
DHT dht(dhtPin, DHT22);
float temperature = 0.0;
float humidity = 0.0;
//sensor de humedad de suelo
const uint16_t seco = 1023;
const uint16_t humedo = 577;
const uint8_t sensorPin = 39;
uint16_t sensorReading; 
//sensor deteccion de gas
const uint16_t gasAlto = 900;
const uint16_t gasBajo = 60;
const uint8_t gasPin = 34;
int pinBuzer = 19;
int sensorReadingGas; 
// sensor de distancia
const uint8_t trigPin = 33;
const uint8_t echoPin = 25;
const double soundSpeed = 0.034;
// pines para puertas
Servo myServo;
int servoPin = 22;
int anguloCerrado = 90;
int anguloAbierto = 43;
// declaracion de funciones
void gestionLuces();
void temperaturaAmbiente();
void leerHumedadDeSuelo();
void leerGas();
void leerSensorDistancia();
long getDistance();
void gestionPuerta();
void gestionRiego();

void setup()
{
  Serial.begin(115200);
   analogReadResolution(10);
  // Configuración de pin iluminacion
  pinMode(ledPin, OUTPUT); 
  digitalWrite(ledPin, HIGH); // apagado al inicio
  // configuracion para el pin de reigo
  pinMode(riegoPin, OUTPUT);
  digitalWrite(riegoPin, LOW);
  // pin para sensor de distancia
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
  // pin parea activar buzer
  pinMode(pinBuzer, OUTPUT);

  // Conectar a Wi-Fi
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }

  Serial.println("Conectado a WiFi");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  dht.begin(); // inicialiar el dht
   // Inicializar el servo
  myServo.attach(servoPin);
  myServo.write(anguloCerrado); // Posición inicial del servo (cerrado)


  // rutas
 
  server.on("/data", temperaturaAmbiente); // Ruta para el sensor dht22
  server.on("/humedad/suelo", leerHumedadDeSuelo);
  server.on("/leer/gas", leerGas); 
  server.on("/distancia", leerSensorDistancia);
  server.on("/led/on", gestionLuces); // Ruta para encender iluminacion
  server.on("/led/off", gestionLuces);  // Ruta para apagar iluminacion
  server.on("/riego/on", gestionRiego); // Ruta encender riego
  server.on("/riego/off", gestionRiego); // Ruta para apagar riego
  server.on("/abrir/puerta", gestionPuerta);
  server.on("/cerrar/puerta", gestionPuerta);

  // manejo de cors
  // Manejador global para solicitudes OPTIONS
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(204); // No content
      return;
    }
    server.send(404, "text/plain", "Not found");
  });

  // Iniciar servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}


void gestionLuces(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if(path == "/led/on"){
    digitalWrite(ledPin, LOW);
    server.send(200,"text/plain", "led encendido");
  } else if(path == "/led/off"){
    digitalWrite(ledPin, HIGH);
    server.send(200,"text/plain", "led apagado");
  } else {
    server.send(404,"text/plain", "Not found");
  }
}

void gestionRiego(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if(path == "/riego/on"){
    digitalWrite(riegoPin, HIGH);
    server.send(200,"text/plain", "encendido");
  } else if(path == "/riego/off"){
    digitalWrite(riegoPin, LOW);
    server.send(200,"text/plain", "apagado");
  } else {
    server.send(404,"text/plain", "Not found");
  }
}


void temperaturaAmbiente (){
  
  float temperature = dht.readTemperature();  // Leer temperatura
  float humidity = dht.readHumidity();        // Leer humedad

  // Verificar si los datos son válidos
  if (isnan(temperature) || isnan(humidity)) {
    server.send(500, "text/plain", "Error leyendo el sensor DHT");
    return;
  }

  // Enviar los datos en formato JSON
  String json = "{\"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  Serial.println(json);
}

void leerHumedadDeSuelo (){
 
  static uint32_t prevMillis = 0;
  if(millis() - prevMillis >= sendInterval){
    prevMillis = millis();

    sensorReading = analogRead(sensorPin);

    //Serial.println(sensorReading);
    uint16_t moisturePercentage = map(sensorReading, seco, humedo, 0, 100);

    // Imprimir el porcentaje de humedad mapeado
    Serial.print("Porcentaje de humedad: ");
    Serial.println(moisturePercentage);

    String status;
    if(moisturePercentage < 30){
      status = "Suelo muy seco, Activar sistema de riego!";
    } else if(moisturePercentage >= 30 && moisturePercentage < 80 ){
      status = "Humendad de suelo ideal!";
    } else {
      status = "Suelo demasiado Humedo, Desactive el sistema de riego!";
    }

    // enviar datos en formato JSON
    String json = "{\"humidity\":" + String(moisturePercentage) + ",\"status\":\"" + status + "\"}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);

    Serial.println(json);
    delay(1000);
  }
}

void leerGas(){
 
    sensorReadingGas = analogRead(gasPin);

    Serial.println("datos analogicos del sensor de gas:");
    Serial.println(sensorReadingGas);
    uint16_t lecturaGas = map(sensorReadingGas, gasBajo, gasAlto, 0, 100);
    int porcentajeGas= constrain(lecturaGas, 0, 100); // Limitamos el porcentaje a 0-100
  
    // Imprimir el porcentaje de humedad mapeado
    Serial.println("Porcentaje de gas: ");
    Serial.println(porcentajeGas);

    String status;
    if(porcentajeGas >= 70){
      digitalWrite(pinBuzer, HIGH);
      status = "¡Peligro! Nivel alto de gas detectado.";
    } else {
      digitalWrite(pinBuzer, LOW);
      status = "Nivel de gas en rango seguro.";
    }

    // enviar datos en formato JSON
    String json = "{\"porcentajeGas\": " + String(porcentajeGas) + ",\"status\":\"" + status + "\"}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
    Serial.println("Datos de gas: ");
    Serial.println(json);
    delay(1000);
}


void leerSensorDistancia (){

  static uint32_t prevMillis = 0;
  if(millis() - prevMillis >= dataTxTimeInterval){
    prevMillis = millis();

    long dist = getDistance();

    if(dist < 0){
      server.send(500, "application/json", "{\"error\": \"Error en la medición de distancia\"}");
    }else {
      String json = "{\"distance\": " + String(dist)+ "}";
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", json);
      Serial.println(json);
    }

  }
}

long getDistance(){
    long duration, distance;
    digitalWrite(trigPin, LOW);           // set the trigPin low
    delayMicroseconds(5);                 // wait for 2 microseconds
    digitalWrite(trigPin, HIGH);          // set the trigPin high
    delayMicroseconds(10);                // wait for 10 microseconds
    digitalWrite(trigPin, LOW);          // set the trigPin low

    duration = pulseIn(echoPin, HIGH);    // read the duration of the pulse
    Serial.print("Duración del pulso: ");
    Serial.println(duration);  // Agregamos depuración para ver la duración del pulso
    distance = duration * soundSpeed / 2; // calculate the distance in centimeters
    distance = distance > 350 ? 200 : distance;
    Serial.print("Distancia calculada: ");
    Serial.println(distance);  // Agregamos depuración para ver la distancia calculada
    return distance;     
}

void gestionPuerta (){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if(path == "/abrir/puerta"){
    myServo.write(anguloAbierto);
    server.send(200, "text/plain", "Techo abierto a 90 grados");
  } else if (path == "/cerrar/puerta"){
    myServo.write(anguloCerrado);
    server.send(200, "text/plain", "Techo cerrado a 40 grados");
  } else {
     server.send(404,"text/plain", "Not found");
  }
}

void loop(){
  server.handleClient(); // Manejar las peticiones HTTP
}


