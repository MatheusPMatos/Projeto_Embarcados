#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include "mbedtls/md.h"

// === CONFIGURAÇÕES ===
const char* WIFI_SSID = "IFPR-IoT";
const char* WIFI_PASS = "j^SFDRy5v6470kKHD7";

const char* API_BASE = "http://192.168.201.176:42000/api";
const char* SECRET_KEY = "chave_super_secreta_padrao";  // mesma usada na API

#define SS_PIN 5
#define RST_PIN 22
#define ENROLL_BUTTON_PIN 4

MFRC522 mfrc522(SS_PIN, RST_PIN);
bool enrollMode = false;
String currentUserId = "0";
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 250;

// --- Função HMAC-SHA256 ---
String hmacSha256(const String &data, const String &key) {
  unsigned char hash[32];
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_md_hmac_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  char hexOutput[65];
  for (int i = 0; i < 32; i++) sprintf(hexOutput + i * 2, "%02x", hash[i]);
  hexOutput[64] = 0;
  return String(hexOutput);
}

// --- Gera UUID v4 ---
String generateUUIDv4() {
  uint8_t uuid[16];
  for (int i = 0; i < 16; i++) uuid[i] = esp_random() & 0xFF;
  uuid[6] = (uuid[6] & 0x0F) | 0x40;
  uuid[8] = (uuid[8] & 0x3F) | 0x80;

  char out[37];
  sprintf(out,
          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
          uuid[0], uuid[1], uuid[2], uuid[3],
          uuid[4], uuid[5],
          uuid[6], uuid[7],
          uuid[8], uuid[9],
          uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

  return String(out);
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  //mfrc522.PCD_Init();
  pinMode(ENROLL_BUTTON_PIN, INPUT_PULLUP);
  esp_random(); // inicializa gerador de aleatórios

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());
}

void loop() {

  // alternar modo cadastro
  // if (digitalRead(ENROLL_BUTTON_PIN) == LOW && millis() - lastDebounce > debounceDelay) {
  //   enrollMode = !enrollMode;
  //   lastDebounce = millis();
  //   if (enrollMode) {
  //     Serial.println("Modo ENROLL ativado. Digite o ID do usuário e pressione ENTER:");
  //   } else {
  //     Serial.println("Modo ENROLL desativado.");
  //   }
  // }

  // ler userId via Serial
  if (enrollMode && Serial.available()) {
    currentUserId = Serial.readStringUntil('\n');
    currentUserId.trim();
    Serial.println("UserId definido: " + currentUserId);
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // construir UID
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  Serial.println("Tag lida: " + uidStr);

  delay(400);

  // if (enrollMode && currentUserId != " ") {
  //   cadastrarTag(uidStr, currentUserId);
  // } else {
  //   registrarLeitura(uidStr);
  // }

  if(uidStr != ""){
    registrarLeitura(uidStr);
  }
  
  mfrc522.PICC_HaltA();
}

void registrarLeitura(String uid) {
  String endpoint = String(API_BASE) + "/readings";
  String uuid = generateUUIDv4();
  long ts = millis();

  String payload = "{\"uuid\":\"" + uuid + "\",\"uid\":\"" + uid + "\",\"timestamp\":" + String(ts) + "}";
  enviarRequisicao(endpoint, payload);
}

void cadastrarTag(String uid, String userId) {
  String endpoint = String(API_BASE) + "/users/" + userId + "/tags";
  String uuid = generateUUIDv4();
  long ts = millis();

  String payload = "{\"uuid\":\"" + uuid + "\",\"uid\":\"" + uid + "\",\"userId\":\"" + userId + "\",\"timestamp\":" + String(ts) + "}";
  enviarRequisicao(endpoint, payload);
}

void enviarRequisicao(String url, String payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, falha ao enviar");
    return;
  }

  String signature = hmacSha256(payload, SECRET_KEY);

  HTTPClient http;
  http.begin(url); 
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Signature", signature);
  http.addHeader("X-Device-ID", "ESP32-RFID");

  int code = http.POST(payload);
  String resp = http.getString();

  Serial.println("POST " + url + " -> " + String(code));
  Serial.println("UUID: " + payload);
  Serial.println("Assinatura: " + signature);
  Serial.println("Resposta: " + resp);
  http.end();
}
