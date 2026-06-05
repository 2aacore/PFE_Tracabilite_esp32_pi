import os
import json
from flask import Flask, render_template, request, redirect, url_for, jsonify
from flask_sqlalchemy import SQLAlchemy
import paho.mqtt.client as mqtt

# initialiser l'application Flask
app = Flask(__name__)

# definer le chemin absolu pour le fichier de base de données
db_path = os.path.join(os.path.abspath(os.path.dirname(__file__)), 'chariots.db')
app.config['SQLALCHEMY_DATABASE_URI'] = f'sqlite:///{db_path}'
# configurer un temps d'attente pour éviter les blocages de la base de données
app.config['SQLALCHEMY_ENGINE_OPTIONS'] = {
    "connect_args" : {"timeout": 20}
}
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)


class Chariot(db.Model):
    id = db.Column(db.String(50), primary_key=True)        
    name = db.Column(db.String(100), nullable=False)       
    fabric = db.Column(db.String(100), default="—")       
    design = db.Column(db.String(100), default="—")        
    length = db.Column(db.String(50), default="0")         
    lot = db.Column(db.String(50), default="—")           
    current_parking = db.Column(db.String(50), default="") 


def init_db():
    db.create_all() 
    if not Chariot.query.filter_by(id="Chariot_1").first():
        db.session.add(Chariot(id="Chariot_1", name="Chariot 1", fabric="—", design="—", length="0", lot="—"))
    if not Chariot.query.filter_by(id="Chariot_2").first():
        db.session.add(Chariot(id="Chariot_2", name="Chariot 2", fabric="—", design="—", length="0", lot="—"))
    db.session.commit() 

MQTT_BROKER = "localhost"     
MQTT_TOPIC = "parking/updates"
RSSI_THRESHOLD = -52 

# fct appelée automatiquement lorsque le script se connecte au broker MQTT
def on_connect(client, userdata, flags, rc):
    print(f"Connected to Mosquitto Broker with result code {rc}")
    client.subscribe(MQTT_TOPIC) # abonnement au canal pour recevoir les données des scanners

# fct appelée automatiquement à chaque réception de message sur le canal
def on_message(client, userdata, msg):
    try:
        # decodage et conversion du message JSON reçu en dictionnaire Python
        payload = json.loads(msg.payload.decode('utf-8'))
        
        scanner_raw = payload.get("scanner")  
        chariot_id = payload.get("chariot")  
        rssi_value = payload.get("rssi", -100) 
        parking_map = {"Parking_A": "PARKING A", "Parking_B": "PARKING B"}
        target_parking = parking_map.get(scanner_raw)
        
        if target_parking:
            # utilisation du contexte d'application Flask pour interagir avec la base de données
            with app.app_context():
                
                # le scanner indique que la place est vide
                if chariot_id == "" or not chariot_id:
                    # recherche les chariots actuellement enregistrés sur ce parking
                    occupied_chariots = Chariot.query.filter_by(current_parking=target_parking).all()
                    if occupied_chariots:
                        for c in occupied_chariots:
                            c.current_parking = "" # liberation de la place en base de données
                        db.session.commit()        # enregistre la modification
                        print(f"🧹 Clear Signal: {target_parking} set to LIBRE.")
                    return
                
                # le signal est capté mais il est trop faible
                if rssi_value < RSSI_THRESHOLD:
                    print(f"⏳ Noise Ignored: {chariot_id} near {target_parking} rejected at weak RSSI: ({rssi_value} dBm).")
                    current_occupier = Chariot.query.filter_by(id=chariot_id, current_parking=target_parking).first()
                    if current_occupier:
                        current_occupier.current_parking = ""
                        db.session.commit()
                        print(f"🧹 Drop Signal: {chariot_id} pulled away from {target_parking}. Set to LIBRE.")
                    return

                other_chariots_here = Chariot.query.filter_by(current_parking=target_parking).all()
                for c in other_chariots_here:
                    if c.id != chariot_id:
                        c.current_parking = ""
                
                chariot = Chariot.query.get(chariot_id)
                if chariot:
                    chariot.current_parking = target_parking
                    db.session.commit()
                    print(f"📍 Confirmed Docking: {chariot_id} locked at {target_parking} (RSSI: {rssi_value} dBm)")
                        
    except Exception as e:
        print(f"Error parsing incoming MQTT framework packet: {e}")

mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message


# affiche l'interface utilisateur 
@app.route('/')
def index():
    db.session.expire_all() 
    chariot_1 = Chariot.query.get("Chariot_1")
    chariot_2 = Chariot.query.get("Chariot_2")
    
    parking_zones = {
        "PARKING A": {"has_chariot": False, "details": None},
        "PARKING B": {"has_chariot": False, "details": None}
    }
    
    for chariot in [chariot_1, chariot_2]:
        if chariot and chariot.current_parking in parking_zones:
            parking_zones[chariot.current_parking]["has_chariot"] = True
            parking_zones[chariot.current_parking]["details"] = chariot

    # renvoyer la page HTML 'index.html' en lui transmettant l'état des parkings
    return render_template('index.html', zones=parking_zones)

# permet à l'opérateur de modifier manuellement la fiche technique du tissu d'un chariot via l'interface
@app.route('/update/<string:chariot_id>', methods=['POST'])
def update_chariot(chariot_id):
    chariot = Chariot.query.get_or_404(chariot_id) # recupere le chariot ou renvoie une erreur 404
    chariot.fabric = request.form.get('fabric', '—')
    chariot.design = request.form.get('design', '—')
    chariot.length = request.form.get('length', '0')
    chariot.lot = request.form.get('lot', '—')
    
    db.session.commit()
    return redirect(url_for('index')) 

if __name__ == '__main__':
    with app.app_context():
        init_db() # initliase la base SQLite au lancement
    
    try:
        mqtt_client.connect(MQTT_BROKER, 1883, 60)
        mqtt_client.loop_start() # démarrage du thread d'écoute pour le MQTT
    except Exception as e:
        print(f"Warning: Could not spin up MQTT Background Loop Engine: {e}")
        
    # lance le serveur accessible sur le port 5000
    app.run(host='0.0.0.0', port=5000, debug=True)

# ferme proprement la session 
@app.teardown_appcontext
def shutdown_session(exception=None):
    db.session.remove()
