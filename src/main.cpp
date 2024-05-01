#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <FirebaseESP8266.h> 
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyBn2mbRAikL5ZfiQcf9sn-qfsng4NoOBeU"
#define USER_EMAIL "abinmkabin@gmail.com"
#define USER_PASSWORD "Abagp@211213#"
#define DATABASE_URL "https://awaconwtc-default-rtdb.firebaseio.com/"


//USER CREDENTIALS
const String userName ="abin";
#define WIFI_SSID "SCOPUS"
#define WIFI_PASSWORD "ABAGP@211213"
//USER CREDENTIALS

const int trigPin = D7; // Trig Pin of SR04 connected to D7 (GPIO13)
const int echoPin = D8; // Echo Pin of SR04 connected to D8 (GPIO15)
const int ledPin = D4;  // D1 corresponds to GPIO5 on ESP8266

int level = 5;
int variation = 0;
int high_err_count = 0;
int distance = 0;
int currDistance = 0;
int prevDistance = 0;

int flag_Power_ON = 1;
bool Motor_State = false;

FirebaseAuth auth;
FirebaseConfig config;

String uid;
String databasePath;
String sensorPath = "/Distance";
String timePath = "/Timestamp";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned long sendDataPrevMillis = 0;
unsigned long delayValue = 60000; // Default delay value in milliseconds
FirebaseData firebaseData;

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    digitalWrite(ledPin, HIGH);
    delay(1000);
    digitalWrite(ledPin, LOW);
  }
  digitalWrite(ledPin, HIGH);
  Serial.println(WiFi.localIP());
  Serial.println();
}
unsigned long getTime() {
  timeClient.update();
  return timeClient.getEpochTime() + 19800; // Add 5 hours 30 minutes in seconds for IST
}


long calculateDistance()
{
  long duration, distance;
  delay(1000);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2; // Speed of sound is 34 cm/ms
  return distance;
}

void calibrateInitialData(int levelVal)
{
  long sum = 0;
  for(int i=0;i<levelVal;i++)
  {
    sum+= calculateDistance();
    Serial.print("calibrating dataset level:");
    Serial.println(levelVal);
  }
  currDistance = sum/levelVal;
  prevDistance = sum/levelVal;
}

void setup() {
  Serial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);
  initWiFi();
  timeClient.begin();

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  Firebase.reconnectWiFi(true);
  firebaseData.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback;
  config.max_token_generation_retry = 5;

  Firebase.begin(&config, &auth);

  Serial.println("Getting User UID");
  while (auth.token.uid == "") {
    Serial.print('.');
    delay(1000);
  }
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  databasePath = userName+ "/readings";
  Serial.println("User: " + userName);
  Serial.print("Delay Value: ");
  Serial.println(delayValue);
  calibrateInitialData(20);
}

bool checkInternetConnection() {
  WiFiClient client;
  if (!client.connect("www.google.com", 80)) {
    digitalWrite(ledPin, HIGH);
    return false;
  }
  client.stop();
  digitalWrite(ledPin, LOW);
  return true;
}

bool validateData(int levelVal)
{
  variation = abs(distance-prevDistance);
  if(abs(distance-prevDistance) <= levelVal)
  return true;

  return false;
}

FirebaseJson setJSON(String distance ,long timestamp)
{
  FirebaseJson json;
  json.set(timePath, timestamp); // Set current time
  json.set(sensorPath.c_str(),distance);
  return json;
}

void sendDataCloud(FirebaseJson json,long timestamp)
{
    String dayPath;
    String childPath;
    char childPathBuffer[100];

    time_t tstamp = timestamp; // Convert unsigned long to time_t
    struct tm *timeinfo;
    timeinfo = localtime(&tstamp); // Pass the pointer to time_t variable
    int hour = timeinfo->tm_hour;
    int minute = timeinfo->tm_min;
    int day = timeinfo->tm_mday;
    int second = timeinfo->tm_sec;

    dayPath = databasePath + "/Day" + String(day);
    sprintf(childPathBuffer, "%02d:%02d", hour, minute);
    childPath = String(dayPath + "/" + childPathBuffer);
    
   // Update data in Firebase
    if (Firebase.setJSON(firebaseData, childPath, json)) {
      Serial.println("Data updated successfully");
      digitalWrite(ledPin, HIGH);
      delay(1000);
      digitalWrite(ledPin, LOW);
    } else {
      Serial.print("Error updating data: ");
      Serial.println(firebaseData.errorReason());
    }
}

void processData()
{
  distance = calculateDistance();
  if(validateData(level) || Motor_State)// for now Motor_State always set to false
  {
    high_err_count = 0;
    prevDistance = currDistance;
    currDistance = distance;
  }
  else{
    Serial.print("Error data! --> ");
    Serial.print(distance);
    Serial.println(" cm");
    Serial.print("Data variation! --> ");
    Serial.print(variation);
    Serial.println(" cm");
    if(validateData(level+5))
    {
      prevDistance = distance;
    }
    else if(variation>10)
    {
      high_err_count++;
      if(high_err_count>=2)
      {
        Serial.print("High Error data! --> ");
        Serial.print(distance);
        Serial.println(" cm");
        Serial.print("Data variation! --> ");
        Serial.print(variation);
        Serial.println(" cm");
        //Motor off wait for calibration
        //Rare chance
        calibrateInitialData(3);
      }
    }
  }
  Serial.print("Distance: ");
  Serial.print(currDistance);
  Serial.println(" cm");

}
void checkFirebaseDataUpdate()
{
  if (Firebase.ready() && (millis() - sendDataPrevMillis > delayValue || sendDataPrevMillis == 0)) {
    FirebaseJson json;
    sendDataPrevMillis = millis();
    unsigned long timestamp = getTime();// Get current time

    //wait for valid timestamp
    while (timestamp < 30000) {
      timestamp = getTime();
      Serial.println("Timestamp calibration..");
    }

    //Set json with valid sensor data or ERR_PWR data with timestamp   
    (flag_Power_ON!=1)?json = setJSON(String(currDistance),timestamp):json = setJSON("ERR_PWR",timestamp);
    flag_Power_ON = 0;
    sendDataCloud(json,timestamp);
  }
}

void loop() {
  checkInternetConnection();//data communication error
  processData();//validate and update sensor data
  checkFirebaseDataUpdate();//check if firebase availability and send data to cloud
}


