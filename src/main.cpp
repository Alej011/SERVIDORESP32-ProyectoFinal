#include <Arduino.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WebSocketsServer.h>
#include <Fuzzy.h>

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
// pin para sensores
//  DHT22
const uint8_t dhtPin = 26;
DHT dht(dhtPin, DHT22);
float temperature = 0.0;
float humidity = 0.0;
// sensor de humedad de suelo
const uint16_t seco = 1023;
const uint16_t humedo = 577;
const uint8_t sensorPin = 39;
uint16_t sensorReading;
// sensor deteccion de gas
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
// logica difusa
Fuzzy *fuzzy = new Fuzzy();
bool modo_automatico = false;

// declaracion de funciones
void gestionLuces();
void temperaturaAmbiente();
void leerHumedadDeSuelo();
void leerGas();
void leerSensorDistancia();
long getDistance();
void gestionPuerta();
void gestionRiego();
void aplicarLogicaDifusa(int moisturePercentage);
void aplicarLogicaDifusaIluminacion(float temperatura, float humedad);
void setupFuzzy();

void setup()
{
  Serial.begin(115200);
  analogReadResolution(10);

  Serial.println("Libreria fuzzy");
  // Configuración de pin iluminacion4
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
  server.on("/led/on", gestionLuces);    // Ruta para encender iluminacion
  server.on("/led/off", gestionLuces);   // Ruta para apagar iluminacion
  server.on("/riego/on", gestionRiego);  // Ruta encender riego
  server.on("/riego/off", gestionRiego); // Ruta para apagar riego
  server.on("/abrir/puerta", gestionPuerta);
  server.on("/cerrar/puerta", gestionPuerta);

  // manejo de cors
  // Manejador global para solicitudes OPTIONS
  server.onNotFound([]()
                    {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(204); // No content
      return;
    }
    server.send(404, "text/plain", "Not found"); });

  // Iniciar servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado...");
}

// reglas de logica difusa
void setupFuzzy()
{
  // logica difusa para humedad del suelo
  FuzzySet *muySeco = new FuzzySet(0, 0, 15, 30);
  FuzzySet *normal = new FuzzySet(20, 30, 50, 60);
  FuzzySet *humedo = new FuzzySet(50, 60, 100, 100);

  FuzzySet *riegoOff = new FuzzySet(0, 0, 20, 40);
  FuzzySet *riegoOn = new FuzzySet(30, 60, 100, 100);

  FuzzyInput *humedadSuelo = new FuzzyInput(1);
  humedadSuelo->addFuzzySet(muySeco);
  humedadSuelo->addFuzzySet(normal);
  humedadSuelo->addFuzzySet(humedo);
  fuzzy->addFuzzyInput(humedadSuelo);

  FuzzyOutput *riego = new FuzzyOutput(1);
  riego->addFuzzySet(riegoOff);
  riego->addFuzzySet(riegoOn);
  fuzzy->addFuzzyOutput(riego);

  FuzzyRuleAntecedent *sueloMuySeco = new FuzzyRuleAntecedent();
  sueloMuySeco->joinSingle(muySeco);

  FuzzyRuleConsequent *activarRiego = new FuzzyRuleConsequent();
  activarRiego->addOutput(riegoOn);

  FuzzyRule *regla1 = new FuzzyRule(1, sueloMuySeco, activarRiego);
  fuzzy->addFuzzyRule(regla1);

  FuzzyRuleAntecedent *sueloHumedo = new FuzzyRuleAntecedent();
  sueloHumedo->joinSingle(humedo);

  FuzzyRuleConsequent *desactivarRiego = new FuzzyRuleConsequent();
  desactivarRiego->addOutput(riegoOff);

  FuzzyRule *regla2 = new FuzzyRule(2, sueloHumedo, desactivarRiego);
  fuzzy->addFuzzyRule(regla2);
  //----------------------------------------/
  // logica difusa para la iluiminacion
  // Conjuntos Difusos para Temperatura
  FuzzySet *bajaTemp = new FuzzySet(0, 0, 15, 20);
  FuzzySet *idealTemp = new FuzzySet(18, 20, 30, 32);
  FuzzySet *altaTemp = new FuzzySet(30, 35, 100, 100);

  // Conjuntos Difusos para Humedad
  FuzzySet *bajaHumedad = new FuzzySet(0, 0, 30, 40);
  FuzzySet *idealHumedad = new FuzzySet(35, 40, 70, 75);
  FuzzySet *altaHumedad = new FuzzySet(70, 80, 100, 100);

  // Conjuntos Difusos para Iluminación
  FuzzySet *luzOff = new FuzzySet(0, 0, 20, 40);
  FuzzySet *luzOn = new FuzzySet(30, 60, 100, 100);

  // Entrada Difusa para Temperatura
  FuzzyInput *temperatura = new FuzzyInput(2);
  temperatura->addFuzzySet(bajaTemp);
  temperatura->addFuzzySet(idealTemp);
  temperatura->addFuzzySet(altaTemp);
  fuzzy->addFuzzyInput(temperatura);

  //  Entrada Difusa para Humedad
  FuzzyInput *humedad = new FuzzyInput(3);
  humedad->addFuzzySet(bajaHumedad);
  humedad->addFuzzySet(idealHumedad);
  humedad->addFuzzySet(altaHumedad);
  fuzzy->addFuzzyInput(humedad);

  //  Salida Difusa para la Iluminación
  FuzzyOutput *iluminacion = new FuzzyOutput(2);
  iluminacion->addFuzzySet(luzOff);
  iluminacion->addFuzzySet(luzOn);
  fuzzy->addFuzzyOutput(iluminacion);

  // Reglas Difusas para la Iluminación

  // Si la temperatura es baja o la humedad es alta, apaga la luz
  FuzzyRuleAntecedent *condicion1 = new FuzzyRuleAntecedent();
  condicion1->joinWithOR(bajaTemp, altaHumedad);

  FuzzyRuleConsequent *apagarLuz = new FuzzyRuleConsequent();
  apagarLuz->addOutput(luzOff);

  FuzzyRule *regla3 = new FuzzyRule(3, condicion1, apagarLuz);
  fuzzy->addFuzzyRule(regla3);

  // Si la temperatura es ideal y la humedad es baja, enciende la luz
  FuzzyRuleAntecedent *condicion2 = new FuzzyRuleAntecedent();
  condicion2->joinWithAND(idealTemp, bajaHumedad);

  FuzzyRuleConsequent *encenderLuz = new FuzzyRuleConsequent();
  encenderLuz->addOutput(luzOn);

  FuzzyRule *regla4 = new FuzzyRule(4, condicion2, encenderLuz);
  fuzzy->addFuzzyRule(regla4);
}

// Aplicar lógica difusa con los parametros de humedad del suelo
void aplicarLogicaDifusa(int moisturePercentage)
{
  fuzzy->setInput(1, moisturePercentage);
  fuzzy->fuzzify();

  int riego = fuzzy->defuzzify(1);

  if (riego > 50)
  {
    digitalWrite(riegoPin, HIGH);
  }
  else
  {
    digitalWrite(riegoPin, LOW);
  }
}

// funcion para logica difusa de iluminacion
void aplicarLogicaDifusaIluminacion(float temperatura, float humedad)
{
  fuzzy->setInput(2, temperatura);
  fuzzy->setInput(3, humedad);
  fuzzy->fuzzify();

  int luz = fuzzy->defuzzify(2);

  if (luz > 50)
  {
    digitalWrite(ledPin, LOW); // Activa la iluminación
    Serial.println("Iluminación activada automáticamente por IA.");
  }
  else
  {
    digitalWrite(ledPin, HIGH); // Apaga la iluminación
    Serial.println("Iluminación desactivada automáticamente por IA.");
  }
}

// funcion para activar la IA
void toggleModoIA()
{
  String input = server.arg("estado");
  if (input == "on")
  {
    modo_automatico = true;
  }
  else if (input == "off")
  {
    modo_automatico = false;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Modo IA actualizado: " + String(modo_automatico ? "ON" : "OFF"));
}

void gestionLuces()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if (path == "/led/on")
  {
    digitalWrite(ledPin, LOW);
    server.send(200, "text/plain", "led encendido");
  }
  else if (path == "/led/off")
  {
    digitalWrite(ledPin, HIGH);
    server.send(200, "text/plain", "led apagado");
  }
  else
  {
    server.send(404, "text/plain", "Not found");
  }
}

void gestionRiego()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if (path == "/riego/on")
  {
    digitalWrite(riegoPin, HIGH);
    server.send(200, "text/plain", "encendido");
  }
  else if (path == "/riego/off")
  {
    digitalWrite(riegoPin, LOW);
    server.send(200, "text/plain", "apagado");
  }
  else
  {
    server.send(404, "text/plain", "Not found");
  }
}

void temperaturaAmbiente()
{

  float temperature = dht.readTemperature(); // Leer temperatura
  float humidity = dht.readHumidity();       // Leer humedad

  // Verificar si los datos son válidos
  if (isnan(temperature) || isnan(humidity))
  {
    server.send(500, "text/plain", "Error leyendo el sensor DHT");
    return;
  }

  // Enviar los datos en formato JSON
  String json = "{\"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  Serial.println(json);

  if (modo_automatico)
  {
    aplicarLogicaDifusaIluminacion(temperature, humidity);
  }
}

void leerHumedadDeSuelo()
{

  static uint32_t prevMillis = 0;
  if (millis() - prevMillis >= sendInterval)
  {
    prevMillis = millis();

    sensorReading = analogRead(sensorPin);

    // Serial.println(sensorReading);
    uint16_t moisturePercentage = map(sensorReading, seco, humedo, 0, 100);

    // Imprimir el porcentaje de humedad mapeado
    Serial.print("Porcentaje de humedad: ");
    Serial.println(moisturePercentage);

    String status;
    if (moisturePercentage < 30)
    {
      status = "Suelo muy seco, Activar sistema de riego!";
    }
    else if (moisturePercentage >= 30 && moisturePercentage < 80)
    {
      status = "Humendad de suelo ideal!";
    }
    else
    {
      status = "Suelo demasiado Humedo, Desactive el sistema de riego!";
    }

    // enviar datos en formato JSON
    String json = "{\"humidity\":" + String(moisturePercentage) + ",\"status\":\"" + status + "\"}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);

    Serial.println(json);
    delay(1000);

    if (modo_automatico)
    {
      aplicarLogicaDifusa(moisturePercentage);
    }
  }
}

void leerGas()
{

  sensorReadingGas = analogRead(gasPin);

  Serial.println("datos analogicos del sensor de gas:");
  Serial.println(sensorReadingGas);
  uint16_t lecturaGas = map(sensorReadingGas, gasBajo, gasAlto, 0, 100);
  // int porcentajeGas= constrain(lecturaGas, 0, 100); // Limitamos el porcentaje a 0-100

  // Imprimir el porcentaje de humedad mapeado
  Serial.println("Porcentaje de gas: ");
  Serial.println(lecturaGas);

  String status;
  if (lecturaGas >= 70)
  {
    digitalWrite(pinBuzer, HIGH);
    status = "¡Peligro! Nivel alto de gas detectado.";
  }
  else
  {
    digitalWrite(pinBuzer, LOW);
    status = "Nivel de gas en rango seguro.";
  }

  // enviar datos en formato JSON
  String json = "{\"porcentajeGas\": " + String(lecturaGas) + ",\"status\":\"" + status + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  Serial.println("Datos de gas: ");
  Serial.println(json);
  delay(1000);
}

void leerSensorDistancia()
{

  static uint32_t prevMillis = 0;
  if (millis() - prevMillis >= dataTxTimeInterval)
  {
    prevMillis = millis();

    long dist = getDistance();

    if (dist < 0)
    {
      server.send(500, "application/json", "{\"error\": \"Error en la medición de distancia\"}");
    }
    else
    {
      String json = "{\"distance\": " + String(dist) + "}";
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", json);
      Serial.println(json);
    }
  }
}

long getDistance()
{
  long duration, distance;
  digitalWrite(trigPin, LOW);  // set the trigPin low
  delayMicroseconds(5);        // wait for 2 microseconds
  digitalWrite(trigPin, HIGH); // set the trigPin high
  delayMicroseconds(10);       // wait for 10 microseconds
  digitalWrite(trigPin, LOW);  // set the trigPin low

  duration = pulseIn(echoPin, HIGH); // read the duration of the pulse
  Serial.print("Duración del pulso: ");
  Serial.println(duration);             // Agregamos depuración para ver la duración del pulso
  distance = duration * soundSpeed / 2; // calculate the distance in centimeters
  distance = distance > 350 ? 200 : distance;
  Serial.print("Distancia calculada: ");
  Serial.println(distance); // Agregamos depuración para ver la distancia calculada
  return distance;
}

void gestionPuerta()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String path = server.uri();

  if (path == "/abrir/puerta")
  {
    myServo.write(anguloAbierto);
    server.send(200, "text/plain", "Techo abierto a 90 grados");
  }
  else if (path == "/cerrar/puerta")
  {
    myServo.write(anguloCerrado);
    server.send(200, "text/plain", "Techo cerrado a 40 grados");
  }
  else
  {
    server.send(404, "text/plain", "Not found");
  }
}

void loop()
{
  server.handleClient(); // Manejar las peticiones HTTP
}
