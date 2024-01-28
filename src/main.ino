#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo servo;

const char* wifiSsid = "Wokwi-GUEST";
const char* wifiPassword = "";
const char* mqttServer = "broker.emqx.io";
const int mqttPort = 1883;

const int ledPin =  23;
const int dhtPin =  13;
const int servoPin =  18;
#define LDRPIN 34 

const float gama = 0.7;
const float rl10 = 30;
int pos = 0;

const char* tempTopic = "ESP32/temperature";
const char* humTopic = "ESP32/humidity";
const char* controlTopic = "ESP32/sensorcontrol";
const char* statusTopic = "ESP32/status";
const char* weatherTopic = "ESP32/light";
const char* luxTopic = "ESP32/lux";

const int sensorReadingInterval = 1000;

//wifi client dan mqtt client
WiFiClient espClient;
PubSubClient client(espClient);
DHT dhtSensor(dhtPin, DHT22);

unsigned long lastSensorReadingTime = 0;
bool activeStatus = false;

void showStatus(bool status) {
    String statusString;
    if (status) {
        digitalWrite(ledPin, HIGH);
        lcd.backlight();
        lcd.setCursor(3, 0);
        lcd.print("ALAT PENYIRAM");
        lcd.setCursor(2, 1);
        lcd.print("TANAMAN OTOMATIS");
        lcd.setCursor(6, 2);
        lcd.print("MENYALA");
        statusString = "ON";
    } else {
        digitalWrite(ledPin, LOW);
        lcd.noBacklight();
        lcd.clear();
        statusString = "OFF";
    }
    Serial.printf("Sensor Status: %s\r\n", statusString.c_str());
    char statusChar[4];
    sniprintf(statusChar, 4, "%s", statusString.c_str());
    
    //publish status to mqtt server
    client.publish(statusTopic, statusChar); 
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("Message received: %s\r\n", topic);
    char statusInput = (char)payload[0];
    Serial.printf("Payload: %c\r\n", statusInput);

    //check if message is on or off
    if (statusInput == '0') {
        activeStatus = false;
    } else if (statusInput == '1') {
        activeStatus = true;
    }

    //call show status function
    showStatus(activeStatus);
}

void mqttConnect() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT server...");
        Serial.println(mqttServer);

        char clientId[20];
        sprintf(clientId, "ESP32Client-%d", random(1000));
        bool isConnect = client.connect(clientId);
        if(isConnect) {
            Serial.println("Connected");
            client.subscribe(controlTopic);
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void wifiConnect() {
    //connect to wifi
    WiFi.begin(wifiSsid, wifiPassword);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
}

void setup() {
    pinMode(ledPin, OUTPUT);
    pinMode(LDRPIN, INPUT);
    Serial.begin(115200);
    dhtSensor.begin();
    lcd.init();
    servo.attach(servoPin);

    //call wifi connection function
    wifiConnect();
    client.setServer(mqttServer, mqttPort);
    client.setCallback(mqttCallback);
}

void loop() {
    //call mqtt connection function
    if (!client.connected()) {
        mqttConnect();
    }
    client.loop();

    if (activeStatus && (millis() - lastSensorReadingTime) > sensorReadingInterval) {
        lastSensorReadingTime = millis();

        float humidity = dhtSensor.readHumidity();
        float temperature = dhtSensor.readTemperature();

        if (humidity < 60){
            servo.write(90);
        
        } else if (humidity >= 60){ 
            servo.write(0);                          
        }

		
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println("%");

        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println("°C");

        int ldrValue = analogRead(LDRPIN);
        ldrValue = map(ldrValue, 4095, 0, 1024, 0); //change LDR sensor read from ADC value arduino to ADC ESP32 value
        float voltage = ldrValue / 1024.*5;
        float resistance = 2000 * voltage / (1-voltage/5);
        float lux = pow(rl10*1e3*pow(10,gama)/resistance,(1/gama));
        
        Serial.print("Light Intensity: ");
        Serial.print(lux);
        Serial.println(" lux");
        
        String light;
        if (lux < 50) {
            light = "Night Light";
        } else if (lux < 989) {
            light = "Room Light";
        } else if (lux <= 9673){
            light = "Overcast Day";
        } else if (lux <= 80000){
            light = "Day Light";
        } else {
            light = "Direct Sunlight";
        }

        //publish humidity and temperature to mqtt server
        char statusMessage[16];
        snprintf(statusMessage, 5, "%f", humidity);
        client.publish(humTopic, statusMessage);
        snprintf(statusMessage, 5, "%f", temperature);
        client.publish(tempTopic, statusMessage);
        sniprintf(statusMessage, 16, "%s", light.c_str());
        client.publish(weatherTopic, statusMessage);
        snprintf(statusMessage, 9, "%f", lux);
        client.publish(luxTopic, statusMessage);
    }
}