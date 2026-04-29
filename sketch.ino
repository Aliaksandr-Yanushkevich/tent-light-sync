#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

// ================== PINS ==================
#define PIN_PWM     4
#define PIN_DETECT  5

// ================== PWM ==================
#define PWM_FREQ    1000
#define PWM_RES     10
#define PWM_MAX     ((1 << PWM_RES) - 1)

// ================== BREATH ==================
#define MODE_BREATH 1

// Яркость 0..1023
#define DEFAULT_MIN_BRIGHTNESS 80
#define DEFAULT_MAX_BRIGHTNESS 400
#define DEFAULT_PERIOD_MS      10000

// ================== ESP-NOW ==================
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

typedef struct {
  uint8_t version;
  uint8_t mode;
  uint16_t period_ms;
  uint16_t min_brightness;
  uint16_t max_brightness;
  uint32_t master_time;
  uint32_t sequence;
} SyncPacket;

SyncPacket current = {
  1,
  MODE_BREATH,
  DEFAULT_PERIOD_MS,
  DEFAULT_MIN_BRIGHTNESS,
  DEFAULT_MAX_BRIGHTNESS,
  0,
  0
};

// ================== STATE ==================
bool isMaster = false;
bool wasConnected = false;

uint32_t localStartTime = 0;
uint32_t lastSyncTime = 0;
uint32_t lastSendTime = 0;
uint32_t sequenceCounter = 0;

// ================== LIGHT ==================
void lightOff() {
  // Важно: НЕ использовать pinMode/digitalWrite на PWM-пине после ledcAttach()
  ledcWrite(PIN_PWM, 0);
}

void setBrightness(uint16_t value) {
  if (value > PWM_MAX) value = PWM_MAX;
  ledcWrite(PIN_PWM, value);
}

void updateBreath() {
  if (current.period_ms == 0) current.period_ms = DEFAULT_PERIOD_MS;

  uint32_t t = millis() - localStartTime;

  float phase = (float)(t % current.period_ms) / current.period_ms;
  float s = (sin(phase * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;

  uint16_t brightness =
    current.min_brightness +
    s * (current.max_brightness - current.min_brightness);

  setBrightness(brightness);
}

// ================== ESP-NOW RECEIVE ==================
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(SyncPacket)) return;

  SyncPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  if (incoming.version != 1) return;
  if (incoming.mode != MODE_BREATH) return;

  current = incoming;

  isMaster = false;
  lastSyncTime = millis();

  // Синхронизация фазы дыхания
  localStartTime = millis() - current.master_time;

  Serial.print("SYNC RX seq=");
  Serial.println(current.sequence);
}

// ================== ESP-NOW INIT ==================
void initESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) == ESP_OK) {
    Serial.println("Broadcast peer added");
  } else {
    Serial.println("Failed to add broadcast peer");
  }
}

// ================== ESP-NOW SEND ==================
void sendSync() {
  current.master_time = millis() - localStartTime;
  current.sequence = sequenceCounter++;

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&current, sizeof(current));

  if (result == ESP_OK) {
    Serial.print("SYNC TX seq=");
    Serial.println(current.sequence);
  } else {
    Serial.print("SYNC FAIL code=");
    Serial.println(result);
  }
}

// ================== ROLE ==================
void becomeMaster() {
  isMaster = true;
  localStartTime = millis();
  lastSendTime = 0;

  Serial.println("ROLE: MASTER");
}

void listenForMaster() {
  Serial.println("Listening for master 3 sec...");

  uint32_t start = millis();
  while (millis() - start < 3000) {
    delay(10);
  }

  if (lastSyncTime == 0) {
    becomeMaster();
  } else {
    isMaster = false;
    Serial.println("ROLE: SLAVE");
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== tent-light-sync v1 ===");

  pinMode(PIN_DETECT, INPUT_PULLUP);

  ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
  lightOff();

  initESPNow();

  if (digitalRead(PIN_DETECT) == LOW) {
    wasConnected = true;
    Serial.println("DETECT: connected");
    listenForMaster();
  } else {
    wasConnected = false;
    Serial.println("DETECT: not connected, light OFF");
    lightOff();
  }
}

// ================== LOOP ==================
void loop() {
  bool connected = digitalRead(PIN_DETECT) == LOW;

  // Разъём отключён
  if (!connected) {
    lightOff();
    wasConnected = false;
    delay(50);
    return;
  }

  // Разъём только что подключили
  if (connected && !wasConnected) {
    wasConnected = true;
    lastSyncTime = 0;
    listenForMaster();
  }

  // Если slave потерял master
  if (!isMaster && millis() - lastSyncTime > 30000) {
    Serial.println("MASTER LOST");
    becomeMaster();
  }

  updateBreath();

  // Master шлёт sync
  if (isMaster && millis() - lastSendTime > 10000) {
    sendSync();
    lastSendTime = millis();
  }

  delay(10);
}
