#include <Arduino.h>
#include <WiFi.h>
#include "WiFiManagerESP32.h"

#include "core/LineReader.h"
#include "config/MqttConfig.h"
#include "config/MqttConfigStore.h"
#include "mqtt/MqttChat.h"
#include "ui/MqttMenuController.h"

// Silenciar logs IDF (opcional)
#include "esp_log.h"
#ifndef MUTE_IDF_LOGS
#define MUTE_IDF_LOGS 0
#endif
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef SERIAL_CLI_TIMEOUT_MS
#define SERIAL_CLI_TIMEOUT_MS 120000UL
#endif

// ---- Globales/inyectables ----
WiFiManagerESP32 wifi;
MqttConfig       cfg;
MqttConfigStore  cfgStore("mqtt");
MqttChat         chat(cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.pass.c_str(), cfg.topic.c_str());
LineReader       reader(&Serial);
MqttMenuController* mqttCtl = nullptr;

// Anexo bajo el menú Wi-Fi
static void printMqttAnnex() {
  Serial.println(F("===== MQTT MENU ====="));
  Serial.println(F("[y] Abrir menú MQTT"));
  Serial.println();
  Serial.print(F("> "));
}

// Muestra ambos menús (Wi-Fi + anexo MQTT)
static void showRootMenusOnce() {
  wifi.showMenu();   // imprime el menú Wi-Fi y su prompt
  printMqttAnnex();  // añadimos el bloque MQTT y prompt
}
static void safeShowMenusTwice() {
  showRootMenusOnce();
  delay(250);
  showRootMenusOnce();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
#if MUTE_IDF_LOGS
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.setDebugOutput(false);
#endif

  // Espera a que el monitor agarre
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 2500) { delay(10); }
  delay(150);

  Serial.println();
  Serial.println(F("=== Riego ESP32 - WiFi + MQTT (SOLID) ==="));

  // Cargar configuración MQTT persistida
  cfgStore.load(cfg);

  // Iniciar Wi-Fi
  wifi.begin(&Serial, SERIAL_CLI_TIMEOUT_MS);

  // Preparar chat con cfg (por si cambió al cargar)
  chat.setServer(cfg.host, cfg.port);
  chat.setAuth(cfg.user, cfg.pass);
  chat.setTopic(cfg.topic);
  chat.setSubTopic(cfg.subTopic);   // NUEVO
  chat.begin();

  // Crear controller del menú MQTT
  static MqttMenuController ctl(Serial, reader, cfg, cfgStore, chat);
  mqttCtl = &ctl;

  // Menús raíz (doble impresión por robustez del monitor serie)
  safeShowMenusTwice();
}

void loop() {
  // Si NO estamos en el menú MQTT…
  if (!mqttCtl->isActive()) {
    bool appendAnnexAfterHandle = false;

    // Interceptar atajo 'y'/'Y' para abrir el menú MQTT
    if (Serial.available()) {
      int pk = Serial.peek();
      if (pk == 'y' || pk == 'Y') {
        String l;
        if (reader.poll(l)) {   // consume la línea "y"
          mqttCtl->enter();
          return;
        }
      }
      // Si reimprimen menú Wi-Fi con 'q', anexamos bloque MQTT luego
      if (pk == 'q' || pk == 'Q') {
        appendAnnexAfterHandle = true;
      }
    }

    // Flujo normal del WiFiManager
    wifi.handleSerial();
    wifi.service();

    if (appendAnnexAfterHandle) {
      printMqttAnnex();
    }

    // Mantén MQTT vivo aunque no estés en el menú
    if (wifi.isConnected()) chat.loop();

    delay(2);
    return;
  }

  // Si SÍ estamos en el menú MQTT…
  wifi.service();
  mqttCtl->loop(wifi.isConnected());

  // Si el controller decidió salir, reimprime menús raíz
  if (!mqttCtl->isActive()) {
    safeShowMenusTwice();
  }

  delay(2);
}
