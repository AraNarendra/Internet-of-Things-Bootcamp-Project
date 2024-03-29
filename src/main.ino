#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include <ESP_Mail_Client.h>
#include <email.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo servo;

const char* wifiSsid = "Wokwi-GUEST";
const char* wifiPassword = "";
const char* mqttServer = "broker.emqx.io";
const int mqttPort = 1883;

//declare for SMTP gmail
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

//The sign in credentials
#define AUTHOR_EMAIL "ENTER SENDER EMAIL ADDRESS HERE"
#define AUTHOR_PASSWORD "ENTER SENDER EMAIL APP PASSWORD HERE"

//Recipient's email
#define RECIPIENT_EMAIL "ENTER RECIPIENT EMAIL ADDRESS HERE"

SMTPSession smtp; //Declare the SMTPSession object for SMTP transport
SMTP_Message message; //Declare the message class
Session_Config config; //Declare the Session_Config for user defined session credentials

const int ledPin =  23;
const int dhtPin =  13;
const int servoPin =  18;
#define LDRPIN 34 

const float gama = 0.7;
const float rl10 = 50;

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
bool isSent = false;
bool servoTurnOn = false;

void smtpCallback(SMTP_Status status);

void smtpConnect(){
    //Set the session config
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = AUTHOR_EMAIL;
    config.login.password = AUTHOR_PASSWORD;
    config.login.user_domain = "";
    
    config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
    config.time.gmt_offset = 3;
    config.time.day_light_offset = 0;

    //Set the message headers
    message.sender.name = F("ESP32 Plant Monitoring System");
    message.sender.email = AUTHOR_EMAIL;
    message.subject = F("Peringatan Kondisi Tanaman");
    message.addRecipient(F("Mr./Mrs."), RECIPIENT_EMAIL);

    //Send HTML message
    String htmlMsg = email; 
    message.html.content = htmlMsg.c_str();
    message.html.content = htmlMsg.c_str();
    message.text.charSet = "us-ascii";
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

    ///Connect to the server
    if (!smtp.connect(&config)){
        ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
        return;
    }
}

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
    servo.write(0);

    //call wifi connection function
    wifiConnect();
    client.setServer(mqttServer, mqttPort);
    client.setCallback(mqttCallback);

    //Set the network reconnection option
    MailClient.networkReconnect(true);
    smtp.debug(1);

    //Set the callback function to get the sending results
    smtp.callback(smtpCallback); 

    //call smtp connection function
    smtpConnect(); 
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
        } else if (lux < 1000) {
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

        //control servo motor turn on/off
        if (servoTurnOn == false && humidity < 40){
            servo.write(90);
            servoTurnOn = true;
        } else if (humidity >= 40){ 
            servo.write(0);
            servoTurnOn = false;                          
        }

        //send email if lux is less than 1000
        if (isSent == false && lux < 1000){
            //Connect to the server
            if (!smtp.connect(&config)){
                ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
                return;
            }
            if (!smtp.isLoggedIn()){
                Serial.println("\nNot yet logged in.");
            }
            else{
                if (smtp.isAuthenticated())
                    Serial.println("\nSuccessfully logged in.");
                else
                    Serial.println("\nConnected with no Auth.");
            }

            //Start sending Email and close the session
            if (!MailClient.sendMail(&smtp, &message))
            ESP_MAIL_PRINTF("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
            isSent = true;
        } else if (lux >= 1000){
            isSent = false;
        }
    }
}

void smtpCallback(SMTP_Status status){
    //Print the current status
    Serial.println(status.info());

    //Print the sending result
    if (status.success()){
        Serial.println("----------------");
        ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
        ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
        Serial.println("----------------\n");

        for (size_t i = 0; i < smtp.sendingResult.size(); i++){
            //Get the result item
            SMTP_Result result = smtp.sendingResult.getItem(i);

            ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
            ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
            ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
            ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
            ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
        }
        Serial.println("----------------\n");

        //clear sending result as the memory usage will grow up.
        smtp.sendingResult.clear();
    }
}