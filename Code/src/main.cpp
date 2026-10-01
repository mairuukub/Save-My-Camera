#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Preferences.h>

#define BTN_MODE_PIN 18
#define BTN_SAVE_PIN 19
#define POT_PIN 35
#define TRIG_PIN 33
#define ECHO_PIN 34
#define DHT_PIN 4
#define LED_RED 25
#define LED_YELLOW 26
#define LED_GREEN 27
#define BUZZER_PIN 14

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
Preferences preferences;

enum State { NORMAL, SET_DIST, SET_TEMP, SET_HUM_MIN, SET_HUM_MAX, SET_DELAY };
State currentState = NORMAL;

int savedDist, savedTemp, savedHumMin, savedHumMax, savedDelayMins;
unsigned long cameraDetectedTime = 0;
unsigned long cameraLostTime = 0;
bool isCameraInCabinet = false;
bool isMuted = false;

unsigned long lastDebounceTime_Mode = 0;
unsigned long lastDebounceTime_Save = 0;
bool lastModeState = HIGH;
bool lastSaveState = HIGH;
const int debounceDelay = 50;

unsigned long lastAlarmBlink = 0;
bool alarmBlinkState = false;

unsigned long lastPingTime = 0;
long currentDist = 999;

long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
  if (duration == 0) return 999; 
  return duration * 0.034 / 2;
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_MODE_PIN, INPUT_PULLUP);
  pinMode(BTN_SAVE_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);


  dht.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }

  preferences.begin("cabinet", false);
  
  savedDist = preferences.getInt("dist", 50); 
  savedTemp = preferences.getInt("temp", 38);     
  savedHumMin = preferences.getInt("humMin", 20); 
  savedHumMax = preferences.getInt("humMax", 60); 
  savedDelayMins = preferences.getInt("delay", 1); 
}

void loop() {
  // ปุ่ม Mode
  bool readingMode = digitalRead(BTN_MODE_PIN);
  if (readingMode != lastModeState) lastDebounceTime_Mode = millis();
  if ((millis() - lastDebounceTime_Mode) > debounceDelay) {
    if (readingMode == LOW) {
      currentState = (State)((currentState + 1) % 6);
      digitalWrite(LED_RED, LOW); digitalWrite(LED_YELLOW, LOW); digitalWrite(LED_GREEN, LOW);
      noTone(BUZZER_PIN); 
      digitalWrite(BUZZER_PIN, LOW);
      delay(200);
    }
  }

  lastModeState = readingMode;

  // ปุ่ม Save
  bool readingSave = digitalRead(BTN_SAVE_PIN);
  if (readingSave != lastSaveState) lastDebounceTime_Save = millis();
  bool savePressed = false;
  if ((millis() - lastDebounceTime_Save) > debounceDelay) {
    if (readingSave == LOW) {
      savePressed = true;
      delay(200);
    }
  }
  lastSaveState = readingSave;

  // อ่าน Ultrasonic (ทุกๆ 100ms)
  if (millis() - lastPingTime > 100) {
    currentDist = getDistance();
    lastPingTime = millis();
  }
  
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int potValue = analogRead(POT_PIN);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  switch (currentState) {
    case NORMAL: {
      if (isnan(t) || isnan(h)) {
        digitalWrite(LED_YELLOW, HIGH);
        display.setTextSize(1);
        for(int i = 0; i < 3; i++) {
          display.setCursor(4, 32);
          display.println("--- SENSOR ERROR ---");
          display.display();
          delay(500);
          display.clearDisplay();
          display.display();
          delay(500);
        }
        display.display();
        return; 
      }

      int triggerDistance = savedDist - 5; 

      if (currentDist <= triggerDistance && currentDist > 0) { 
        cameraLostTime = millis(); // อัปเดตเวลาล่าสุดที่ยังเจอกล้องอยู่เสมอ
        if (!isCameraInCabinet) {
          isCameraInCabinet = true;
          cameraDetectedTime = millis();
          isMuted = false;
        }
      } else {
  
        if (isCameraInCabinet && (millis() - cameraLostTime > 2000)) {
          isCameraInCabinet = false;
          digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW); digitalWrite(LED_YELLOW, LOW);
          noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW); 
        }
      }
      
      display.setCursor(0, 0);
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
      display.setTextSize(1); display.setCursor(5, 10);
      display.print("Temperature : "); display.print(t, 1); display.println("C");
      display.setTextSize(1); display.setCursor(5, 20);
      display.print("Humidity    : "); display.print(h, 1); display.println("%");
      
      if (isCameraInCabinet) {
        unsigned long elapsedMillis = millis() - cameraDetectedTime;
        unsigned long delayTargetMillis = savedDelayMins * 60000UL;
        
        unsigned long totalSecs = elapsedMillis / 1000;

        if (elapsedMillis < delayTargetMillis) {
           digitalWrite(LED_GREEN, HIGH);
           noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW); 
           display.setCursor(5, 30);
           display.print("Delay       : ");
           display.print((delayTargetMillis - elapsedMillis) / 1000); display.println(" s");
        } else {
           bool isAlarm = (t >= savedTemp || h >= savedHumMax || h <= savedHumMin);
           bool isWarning = false;
           
           if (!isAlarm) {
             if (t >= savedTemp - 2) isWarning = true;
             if (h >= savedHumMax - 5 || h <= savedHumMin + 5) isWarning = true;
           }
           
           if (isAlarm) {
             if (savePressed) isMuted = true;

             // ลอจิกกระพริบ: สลับสถานะทุกๆ 500 ms (0.5 วินาที)
             if (millis() - lastAlarmBlink >= 500) {
               lastAlarmBlink = millis();
               alarmBlinkState = !alarmBlinkState; // สลับค่า true/false
             }

             // นำสถานะมาคุมการแสดงผล (จอ, ไฟ, เสียง)
             if (alarmBlinkState) {
               digitalWrite(LED_GREEN, LOW); digitalWrite(LED_YELLOW, LOW); digitalWrite(LED_RED, HIGH);
               if (!isMuted) tone(BUZZER_PIN, 1000); 
               else { noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW); }
               
               display.setCursor(12, 30);
               display.println("! ALARM ACTIVE !");
             } else {
               // จังหวะดับ
               digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW); digitalWrite(LED_YELLOW, LOW);
               noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW); 
               
               display.setCursor(12, 30); 
               display.println("                "); 
             }
             
           // --- แทรกสถานะ WARNING ไฟส้ม ---
           } else if (isWarning) {
             digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW); digitalWrite(LED_YELLOW, HIGH);
             noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW);
             display.setCursor(5, 30);
             display.println("Status    : WARNING");
             
           } else {
             digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_RED, LOW); digitalWrite(LED_YELLOW, LOW);
             noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW);
             display.setCursor(5, 30);
             display.println("Status      : SAFE");
           }          
        }
        
        // คำนวณ วัน, ชั่วโมง, นาที, วินาที
        unsigned long d_time = totalSecs / 86400;
        unsigned long h_time = (totalSecs % 86400) / 3600; 
        unsigned long m_time = (totalSecs % 3600) / 60;
        unsigned long s_time = totalSecs % 60;
        
        display.setCursor(5, 40);
        display.print("Time  : "); 
        display.printf("%lud %02lu:%02lu:%02lu\n", d_time, h_time, m_time, s_time);
        
      } else {
        display.setCursor(5, 30);
        display.println("Status  : NO CAMERA");
      }
      break;
    }
    case SET_DIST: {

      int mappedDist = map(potValue, 4095, 0, 10, 100);
      display.setCursor(7,0);
      display.println("-SET DISTANCE (CM)-");
      display.setCursor(8,15);
      display.setTextSize(2);
      display.print("New  : "); display.print(mappedDist); 
      //display.print(" (Raw:"); display.print(potValue); display.println(")");
      display.setTextSize(2); 
      display.setCursor(8,35);
      display.print("Real : "); display.println(currentDist);
      
      // ถ้าหมุนตรงระยะ ให้ติดไฟทั้ง 3 ดวงเช็คหลอดขาด
      if (currentDist > 0 && currentDist != 999) {
        if (currentDist <= mappedDist && currentDist >= mappedDist - 3) {
          digitalWrite(LED_RED, HIGH);
        } else {
          digitalWrite(LED_RED, LOW);
        }
      } else {
        digitalWrite(LED_RED, LOW);
      }

      if (savePressed) {
        savedDist = mappedDist;
        preferences.putInt("dist", savedDist);
        display.println("SAVED!");
      }
      break;
    }
    case SET_TEMP: {
      int mappedTemp = map(potValue, 4095, 0, 20, 60);
      display.setCursor(10,0);
      display.println("SET TEMP LIMIT (C)");
      display.setCursor(8,15);
      display.setTextSize(2);
      display.print("Saved: "); display.println(savedTemp);
      display.setTextSize(2); 
      display.setCursor(8,35);
      display.print("New  : "); display.println(mappedTemp);
      if (savePressed) {
        savedTemp = mappedTemp;
        preferences.putInt("temp", savedTemp);
        display.println("SAVED!");
      }
      break;
    }
    case SET_HUM_MIN: {
      int mappedHumMin = map(potValue, 4095, 0, 20, 50);
      display.setCursor(7,0);
      display.println("--SET HUM MIN (%)--");
      display.setCursor(8,15);
      display.setTextSize(2);
      display.print("Saved: "); display.println(savedHumMin);
      display.setTextSize(2); 
      display.setCursor(8,35);
      display.print("New  : "); display.println(mappedHumMin);
      if (savePressed) {
        savedHumMin = mappedHumMin;
        preferences.putInt("humMin", savedHumMin);
        display.println("SAVED!");
      }
      break;
    }
    case SET_HUM_MAX: {
      int mappedHumMax = map(potValue, 4095, 0, 40, 90);
      display.setCursor(7,0);
      display.println("--SET HUM MAX (%)--");
      display.setCursor(8,15);
      display.setTextSize(2);
      display.print("Saved: "); display.println(savedHumMax);
      display.setTextSize(2); 
      display.setCursor(8,35);
      display.print("New  : "); display.println(mappedHumMax);
      if (savePressed) {
        savedHumMax = mappedHumMax;
        preferences.putInt("humMax", savedHumMax);
        display.println("SAVED!");
      }
      break;
    }
    case SET_DELAY: {
      int mappedDelay = map(potValue, 4095, 0, 0, 60);
      display.setCursor(6,0);
      display.println("--SET DELAY (MINS)--");
      display.setCursor(8,15);
      display.setTextSize(2);
      display.print("Saved: "); display.println(savedDelayMins);
      display.setTextSize(2); 
      display.setCursor(8,35);
      display.print("New  : "); display.println(mappedDelay);
      if (savePressed) {
        savedDelayMins = mappedDelay;
        preferences.putInt("delay", savedDelayMins);
        display.println("SAVED!");
      }
      break;
    }
  }
  display.display();
}