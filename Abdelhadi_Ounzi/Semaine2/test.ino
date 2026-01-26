/*
 * SYSTÈME D'ARROSAGE ULTIME : SOL + DHT22 + GAZ
 * + GESTION AUTO / MANUEL via Node-RED
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// --- 1. CONFIGURATION (Tes paramètres) ---
const char* ssid = "tenda_4465";       
const char* password = "00123456789";    
const char* mqtt_server = "10.167.136.12"; 

// --- DÉFINITION DES BROCHES ---
#define PIN_CAPTEUR_SOL 35
#define PIN_RELAIS 26
#define PIN_MQ2 34        
#define PIN_DHT 4         

#define DHTTYPE DHT22     

// --- RÉGLAGES (Tes calibrations) ---
const int VALEUR_SEC = 2640;     
const int VALEUR_EAU = 1500;
const int SEUIL_ARROSAGE = 20;   // Arrose si < 20% en mode Auto
const int SEUIL_ALERTE_FEU = 2000; // Alerte si gaz > 2000

// --- VARIABLES DE CONTRÔLE ---
bool modeAuto = true;          // Par défaut : Mode Automatique
bool commandeManuelle = false; // Par défaut : Pompe éteinte en manuel

// Objets
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(PIN_DHT, DHTTYPE);

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_CAPTEUR_SOL, INPUT);
  pinMode(PIN_MQ2, INPUT);
  pinMode(PIN_RELAIS, OUTPUT);
  digitalWrite(PIN_RELAIS, LOW); 

  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connecté !");
}

// --- RÉCEPTION DES ORDRES (Le Cerveau qui écoute Node-RED) ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.print("Reçu sur ["); Serial.print(topic); Serial.print("]: "); Serial.println(message);

  // 1. Choix du MODE (Auto ou Manuel)
  if (String(topic) == "agri/mode") {
    if (message == "auto") {
      modeAuto = true;
      Serial.println(">> MODE AUTO ACTIVÉ");
    } else {
      modeAuto = false;
      Serial.println(">> MODE MANUEL ACTIVÉ");
    }
  }

  // 2. Commande de la POMPE (Uniquement utile en mode Manuel)
  if (String(topic) == "agri/pompe") {
    if (message == "on" || message == "true") {
      commandeManuelle = true;
    } else {
      commandeManuelle = false;
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_Final_Client")) {
      Serial.println("Connecté MQTT");
      client.subscribe("agri/mode");   // S'abonne au switch Auto/Manu
      client.subscribe("agri/pompe");  // S'abonne au bouton ON/OFF
    } else { delay(5000); }
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // --- A. LECTURE CAPTEURS ---
  // Sol
  int lectureSol = analogRead(PIN_CAPTEUR_SOL);
  int humSol = map(lectureSol, VALEUR_SEC, VALEUR_EAU, 0, 100);
  humSol = constrain(humSol, 0, 100);

  // Air (DHT22)
  float tempAir = dht.readTemperature();
  float humAir = dht.readHumidity();
  if (isnan(tempAir)) { tempAir=0; humAir=0; }

  // Gaz (MQ2)
  int gaz = analogRead(PIN_MQ2);
  int alerteFeu = (gaz > SEUIL_ALERTE_FEU) ? 1 : 0; // 1 si feu, 0 sinon

  // --- B. LOGIQUE DE LA POMPE (AUTO vs MANUEL) ---
  bool etatPompe = false;

  if (modeAuto == true) {
    // Mode Auto : C'est le sol qui décide
    if (humSol < SEUIL_ARROSAGE) {
      etatPompe = true;
    }
  } else {
    // Mode Manuel : C'est toi qui décides (commandeManuelle)
    etatPompe = commandeManuelle;
  }

  // Application sur le Relais
  if (etatPompe) {
    digitalWrite(PIN_RELAIS, HIGH);
  } else {
    digitalWrite(PIN_RELAIS, LOW);
  }

  Serial.println("--------------------------------");
  Serial.print("🌡️ Température : "); Serial.print(tempAir); Serial.println(" °C");
  Serial.print("💧 Humidité Air: "); Serial.print(humAir); Serial.println(" %");
  Serial.print("🌱 Humidité Sol: "); Serial.print(humSol); Serial.println(" %");
  Serial.print("⚠️ Gaz (Fumée) : "); Serial.print(gaz); 
  if(alerteFeu) Serial.print(" -> 🔥 FEU !");
  Serial.println();
  Serial.print("⚙️ Mode: "); Serial.println(modeAuto ? "AUTO" : "MANUEL");
  Serial.print("🌊 Pompe: "); Serial.println(etatPompe ? "ON" : "OFF");

  // --- C. ENVOI DES DONNÉES VERS NODE-RED ---
  String payload = "{";
  payload += "\"sol\": " + String(humSol) + ",";
  payload += "\"temp\": " + String(tempAir) + ",";
  payload += "\"hum\": " + String(humAir) + ",";
  payload += "\"gaz\": " + String(gaz) + ",";
  payload += "\"feu\": " + String(alerteFeu) + ",";
  // On envoie aussi l'état actuel pour tes voyants
  payload += "\"mode\": \"" + String(modeAuto ? "AUTO" : "MANUEL") + "\",";
  payload += "\"pompe_etat\": " + String(etatPompe); 
  payload += "}";

  client.publish("agri/sensors", payload.c_str());
  Serial.println(payload);

  delay(2000); 
}