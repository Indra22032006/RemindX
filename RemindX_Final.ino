#include <SPI.h>
#include <MFRC522.h>

// ----------------- Pin Definitions -----------------
#define SS_PIN       10
#define RST_PIN      9
#define LED_PIN      5
#define BUZZER_PIN   2       // transistor base
#define BUTTON_PIN   7
#define TRIG_PIN     8
#define ECHO_PIN     4

MFRC522 mfrc522(SS_PIN, RST_PIN);

// ----------------- Stored UIDs -----------------
byte uid1[4] = {0x07, 0xB6, 0x52, 0x8D};
byte uid2[4] = {0xFA, 0x8E, 0xFD, 0xC6};
byte uid3[4] = {0xDC, 0xAC, 0xC1, 0x01};

// ----------------- Global Variables -----------------
bool alertActive = false;
unsigned long lastMotionTime = 0;
const unsigned long motionCooldown = 1500UL;  // motion debounce
const int MOTION_THRESHOLD_CM = 20;

bool card1Scanned = false;
bool card2Scanned = false;
bool card3Scanned = false;
bool vipMode = false;
unsigned long vipStartTime = 0;
const unsigned long vipDuration = 30000UL; // 30s VIP

// Buzzer blink timer
bool buzzerState = false;
unsigned long lastBuzzerToggle = 0;
const unsigned long buzzerInterval = 1000UL; // 1 second

// ----------------- Helper Functions -----------------
bool compareUID(const byte *a, const byte *b, byte size) {
  for (byte i = 0; i < size; i++) if (a[i] != b[i]) return false;
  return true;
}

long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long dur = pulseIn(ECHO_PIN, HIGH, 50000UL);
  if (dur == 0) return -1;
  return dur * 0.034 / 2;
}

bool buttonPressed() {
  static unsigned long lastBounce = 0;
  static int lastState = HIGH;
  int cur = digitalRead(BUTTON_PIN);

  if (cur != lastState) {
    lastBounce = millis();
    lastState = cur;
  }

  if ((millis() - lastBounce) > 40 && cur == LOW) {
    lastState = LOW;
    return true;
  }

  return false;
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("RemindX Ready!");
}

// ----------------- Main Loop -----------------
void loop() {
  unsigned long now = millis();

  // --- Manual VIP mode via button ---
  if (buttonPressed() && !vipMode) {
    vipMode = true;
    vipStartTime = now;
    Serial.println("🎉 VIP Mode manually activated!");
  }

  // --- Automatic VIP mode if all cards scanned ---
  if (!vipMode && card1Scanned && card2Scanned && card3Scanned) {
    vipMode = true;
    vipStartTime = now;
    Serial.println("🎉 VIP Mode activated (all cards scanned)!");
  }

  // --- VIP cooldown expiration ---
  if (vipMode && now - vipStartTime > vipDuration) {
    vipMode = false;
    card1Scanned = card2Scanned = card3Scanned = false;
    Serial.println("VIP cooldown ended. Motion alerts enabled again.");

    // Turn off LED if no alert active
    if (!alertActive) digitalWrite(LED_PIN, LOW);
  }

  // --- Alert handling ---
  if (alertActive) {
    if (buttonPressed()) {
      alertActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, vipMode ? HIGH : LOW);
      Serial.println("Alert acknowledged via button.");
      delay(200);
      return;
    }

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      byte *uid = mfrc522.uid.uidByte;
      if (compareUID(uid, uid1, 4) || compareUID(uid, uid2, 4) || compareUID(uid, uid3, 4)) {
        alertActive = false;
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED_PIN, HIGH); // LED stays ON
        Serial.println("Known card scanned -> alert cleared, LED ON.");
      }
      mfrc522.PICC_HaltA();
      delay(200);
      return;
    }

    // --- Buzzer blinking logic ---
    if (!vipMode) {
      if (now - lastBuzzerToggle >= buzzerInterval) {
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
        lastBuzzerToggle = now;
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW); // VIP disables buzzer
    }

    digitalWrite(LED_PIN, HIGH);
    delay(20);
    return;
  }

  // --- RFID card scan ---
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    byte *uid = mfrc522.uid.uidByte;
    Serial.print("Card UID: ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (uid[i] < 0x10) Serial.print("0");
      Serial.print(uid[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    if (compareUID(uid, uid1, 4)) card1Scanned = true;
    if (compareUID(uid, uid2, 4)) card2Scanned = true;
    if (compareUID(uid, uid3, 4)) card3Scanned = true;

    digitalWrite(LED_PIN, HIGH);
    lastMotionTime = now;

    mfrc522.PICC_HaltA();
    delay(100);
    return;
  }

  // --- Motion detection ---
  if (now - lastMotionTime > motionCooldown) {
    long dist = readDistanceCM();
    if (dist > 0 && dist <= MOTION_THRESHOLD_CM) {
      if (!vipMode) {
        alertActive = true;
        buzzerState = true;
        lastBuzzerToggle = now;
        digitalWrite(BUZZER_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
        Serial.print("🚨 Motion detected (");
        Serial.print(dist);
        Serial.println(" cm) -> buzzer blinking 1s");
      } else {
        Serial.println("Motion detected but VIP active -> buzzer OFF, LED ON");
        digitalWrite(LED_PIN, HIGH);
      }
      lastMotionTime = now;
    }
  }

  delay(40);
}