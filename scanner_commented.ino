#include <WiFi.h>             
#include <PubSubClient.h>      
#include <BLEDevice.h>         
#include <BLEUtils.h>          
#include <BLEScan.h>          
#include <BLEAdvertisedDevice.h> 


const char* ssid         = "exemple_ssid";       
const char* password     = "exemple_mdp";        
const char* mqtt_server  = "x.x.x.x";         // adresse IP fixe de la Raspberry Pi 
const int   mqtt_port    = 1883;                    // port standard utilisé par Mosquitto MQTT
const char* mqtt_topic   = "parking/updates";       // topic MQTT où envoyer les données
const char* scannerID    = "Parking_A";             // nom du esp32 scanner

int scanTimeSeconds = 3;             // durée de chaque période d'écoute 
BLEScan* pBLEScan;                  // pointeur vers l'objet qui gère le scan bluetooth
bool chariotFoundInCurrentScan = false; // indicateur devient vrai si un chariot valide est détecté
String detectedChariot = "";        // stoque le nom du chariot trouvé le plus proche
int detectedRSSI = -100;            // stoque la puissance de signal maximale trouvée 

WiFiClient espClient;               // client réseau pour la couche TCP/IP
PubSubClient mqttClient(espClient); // client MQTT lié à notre connexion Wi-Fi


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str(); // prend le nom de l'appareil détecté
        
        // on ne capte que les appareils qui commencent par 'Chariot_'
        if (name.startsWith("Chariot_")) {
            int currentRSSI = advertisedDevice.getRSSI(); // mesure la force du signal 
            
            // on garde que la balise la plus proche
            if (currentRSSI > detectedRSSI) {
                chariotFoundInCurrentScan = true; // un chariot est officiellement trouvé dans cette session
                detectedChariot = name;          
                detectedRSSI = currentRSSI;      
            }
        }
    }
};



void reconnectMQTT() {
    while (!mqttClient.connected()) { 
        Serial.print("Attempting MQTT connection to Pi...");
        String clientId = "ESP32Scanner-" + String(scannerID); 
        
        if (mqttClient.connect(clientId.c_str())) { 
            Serial.println(" Connected successfully!");
        } else {
            // si l'esp32 ne peut pas se connecter avec le Wi-Fi, elle affiche un code d'erreur et réessaie après 5 secondes
            Serial.print(" failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" | Retrying in 5 seconds...");
            delay(5000);
        }
    }
}

void sendDataToPi(String chariot, int rssi) {
    if (!mqttClient.connected()) {
        reconnectMQTT(); 
    }
    mqttClient.loop(); 

    String jsonPayload = "{\"scanner\":\"" + String(scannerID) + 
                         "\",\"chariot\":\"" + chariot + 
                         "\",\"rssi\":" + String(rssi) + "}";
    

    Serial.print("Publishing MQTT Frame: ");  
    Serial.println(jsonPayload); 
    
    // envoi réel du message sur le réseau vers le serveur Mosquitto
    if (mqttClient.publish(mqtt_topic, jsonPayload.c_str())) {
        Serial.println("Packet pushed successfully"); 
    } else {
        Serial.println("Transmission failed");    
    }
}


void setup() {
    Serial.begin(115200); // démarre le port série pour afficher les logs de débogage
    delay(1000);
    
    Serial.println();
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { // boucle d'attente active jusqu'à confirmation
        delay(500); 
        Serial.print("."); 
    }
    Serial.println("\nWi-Fi Connected!");

    mqttClient.setServer(mqtt_server, mqtt_port);

    Serial.println("Initializing BLE Scanning Subsystem...");
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks()); 
    pBLEScan->setActiveScan(true);       
    pBLEScan->setInterval(100);         
    pBLEScan->setWindow(99);             
}

void loop() {
    if (!mqttClient.connected()) {
        reconnectMQTT(); 
    }
    mqttClient.loop();

    chariotFoundInCurrentScan = false;
    detectedChariot = "";
    detectedRSSI = -100;

    Serial.println("\n--- Starting Active BLE Warehouse Scan ---");

    BLEScanResults* foundDevices = pBLEScan->start(scanTimeSeconds, false);
    
    if (chariotFoundInCurrentScan && detectedRSSI > -75) { 
        Serial.print("Target Chariot Confirmed: "); Serial.print(detectedChariot);
        Serial.print(" | RSSI Value: "); Serial.println(detectedRSSI);

        sendDataToPi(detectedChariot, detectedRSSI);
    } 
    else {
        Serial.println("Parking Empty or Chariot out of range. Sending LIBRE update...");
        sendDataToPi("", -100); 
    }
    
    pBLEScan->clearResults(); // libère la mémoire RAM de l'ESP32 pour éviter les surcharges 
    delay(2000);              // ppause de sécurité de 2 secondes avant le prochain cycle de détection
}