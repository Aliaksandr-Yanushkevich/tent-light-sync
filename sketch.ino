#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

// ================== PINS ==================
#define PIN_PWM 4   // GPIO4 -> 100-220Ω -> Gate MOSFET

// ================== PWM ==================
#define PWM_FREQ 1000
#define PWM_RES  10
#define PWM_MAX  ((1 << PWM_RES) - 1)

// ================== LIGHT SETTINGS ==================
#define MODE_BREATH 1

#define DEFAULT_MIN_BRIGHTNESS 80    // ~8%
#define DEFAULT_MAX_BRIGHTNESS 400   // ~39%
#define DEFAULT_PERIOD_MS      10000 // 10 sec

// ================== TIMING ==================
#define MASTER_SYNC_INTERVAL_MS 3000
#define MASTER_TIMEOUT_MS       30000

// ================== PACKET ==================
typedef struct {
  uint8_t version;
  uint8_t mode;

  uint8_t master_mac[6];

  uint16_t period_ms;
  uint16_t min_brightness;
  uint16_t max_brightness;

  uint32_t elapsed_ms;
  uint32_t sequence;
} SyncPacket;

// ================== GLOBALS ==================
uint8_t ownMac[6];
uint8_t currentMasterMac[6];

uint8_t broadcastAddress[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

bool isMaster = false;

uint32_t localStartTime = 0;
uint32_t lastSyncRxTime = 0;
uint32_t lastSyncTxTime = 0;
uint32_t sequenceCounter = 0;

SyncPacket current = {
  1,
  MODE_BREATH,
  {0, 0, 0, 0, 0, 0},
  DEFAULT_PERIOD_MS,
  DEFAULT_MIN_BRIGHTNESS,
  DEFAULT_MAX_BRIGHTNESS,
  0,
  0
};

// ================== UTILS ==================
int compareMac(const uint8_t *a, const uint8_t *b) {
  for (int i = 0; i < 6; i++) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

void copyMac(uint8_t *dst, const uint8_t *src) {
  memcpy(dst, src, 6);
}

void printMac(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
}

void lightOff() {
  ledcWrite(PIN_PWM, 0);
}

void setBrightness(uint16_t value) {
  if (value > PWM_MAX) value = PWM_MAX;
  ledcWrite(PIN_PWM, value);
}

// ================== BREATH ==================
void updateBreath() {
  if (current.period_ms == 0) {
    current.period_ms = DEFAULT_PERIOD_MS;
  }

  uint32_t t = millis() - localStartTime;
  float phase = (float)(t % current.period_ms) / current.period_ms;

  float s = (sin(phase * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;

  uint16_t brightness =
    current.min_brightness +
    s * (current.max_brightness - current.min_brightness);

  setBrightness(brightness);
}

// ================== ROLE ==================
void becomeMaster() {
  isMaster = true;
  copyMac(currentMasterMac, ownMac);
  copyMac(current.master_mac, ownMac);

  localStartTime = millis();
  lastSyncTxTime = 0;

  Serial.print("ROLE: MASTER, MAC=");
  printMac(ownMac);
  Serial.println();
}

void becomeSlave(const SyncPacket &incoming) {
  isMaster = false;

  current = incoming;
  copyMac(currentMasterMac, incoming.master_mac);

  lastSyncRxTime = millis();
  localStartTime = millis() - incoming.elapsed_ms;

  Serial.print("ROLE: SLAVE, MASTER=");
  printMac(currentMasterMac);
  Serial.print(" seq=");
  Serial.println(current.sequence);
}

// ================== ESP-NOW RECEIVE ==================
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(SyncPacket)) return;

  SyncPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  if (incoming.version != 1) return;
  if (incoming.mode != MODE_BREATH) return;

  if (compareMac(incoming.master_mac, ownMac) == 0 && isMaster) return;

  int cmp = compareMac(incoming.master_mac, currentMasterMac);

  if (cmp < 0) {
    becomeSlave(incoming);
    return;
  }

  if (cmp == 0 && !isMaster) {
    becomeSlave(incoming);
    return;
  }

}

// ================== ESP-NOW SEND ==================
void sendSync() {
  copyMac(current.master_mac, ownMac);

  current.elapsed_ms = millis() - localStartTime;
  current.sequence = sequenceCounter++;

  esp_err_t result = esp_now_send(
    broadcastAddress,
    (uint8_t*)&current,
    sizeof(current)
  );

  if (result == ESP_OK) {
    Serial.print("SYNC TX seq=");
    Serial.println(current.sequence);
  } else {
    Serial.print("SYNC FAIL code=");
    Serial.println(result);
  }
}

// ================== ESP-NOW INIT ==================
void initESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  WiFi.macAddress(ownMac);

  Serial.print("OWN MAC: ");
  printMac(ownMac);
  Serial.println();

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

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== tent-light-sync v1 final ===");

  ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
  lightOff();

  initESPNow();

  becomeMaster();

  Serial.println("Setup complete");
}

// ================== LOOP ==================
void loop() {
  if (!isMaster && millis() - lastSyncRxTime > MASTER_TIMEOUT_MS) {
    Serial.println("MASTER TIMEOUT");
    becomeMaster();
  }

  updateBreath();

  if (isMaster && millis() - lastSyncTxTime > MASTER_SYNC_INTERVAL_MS) {
    sendSync();
    lastSyncTxTime = millis();
  }

  delay(10);
}
