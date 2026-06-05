#include <BLEDevice.h>       
#include <BLEUtils.h>        
#include <BLEAdvertising.h>  

void setup() {
  Serial.begin(115200);      // démarre le port série pour afficher les logs de débogage
  Serial.println("Starting BLE Emetteur..."); 

  BLEDevice::init("Chariot_2");   // initialise le contrôleur ble de l'esp32 et lui donne son nom 
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();// crée un pointeur vers l'objet publicitaire pour configurer l'émission

  pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180F));   // ajoute un UUID pour rendre le paquet ble valide
  pAdvertising->setScanResponse(true);  // active la réponse au scan pour que le scanner puisse lire le nom sans se connecter
  
  // paramètres de connexion pour optimiser la compatibilité avec les récepteurs
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  // lance officiellement la diffusion des paquets bluetooth dans l'air
  BLEDevice::startAdvertising();
  Serial.println("Chariot_2 is now broadcasting its presence continuously..."); 
}

void loop() {
  // la boucle principale reste vide pour ne pas surcharger le processeur
  delay(10000); // attend 10 secondes avant de relancer la boucle vide
}