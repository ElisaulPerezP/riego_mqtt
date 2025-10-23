// File: src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>                 // mDNS: config.local
#include "WiFiManagerESP32.h"

#include "config/MqttConfig.h"
#include "config/MqttConfigStore.h"
#include "mqtt/MqttChat.h"

// Web UI (reemplaza al menú por Serial)
#include "web/WebUI.h"

// Riego (modos) en task para no bloquear
#include "modes/modes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// =================== CONFIG ===================
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
// Timeout muy corto: la CLI de WiFiManager nunca tomará el control
#ifndef SERIAL_CLI_TIMEOUT_MS
#define SERIAL_CLI_TIMEOUT_MS 10UL
#endif

// Selector de modo MANUAL/AUTO por hardware
static constexpr int PIN_SWITCH_MANUAL  = 19;   // LOW=AUTO, HIGH=MANUAL
static constexpr uint32_t MODE_DEBOUNCE_MS = 120;

// =================== “Serial nulo” ===================
// Para que WiFiManagerESP32 no use Serial (GPIO1/3) en absoluto.
class NullStream : public Stream {
public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 1; }
  using Print::write;
};

// =================== GLOBALES ===================
WiFiManagerESP32 wifi;
MqttConfig       cfg;
MqttConfigStore  cfgStore("mqtt");
MqttChat         chat(cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.pass.c_str(), cfg.topic.c_str());
WebUI*           webui = nullptr;

static TaskHandle_t gIrrigationTask = nullptr;

// =================== TASK RIEGO ===================
static void irrigationTask(void* /*pv*/) {
  pinMode(PIN_SWITCH_MANUAL, INPUT_PULLDOWN);

  bool lastRaw    = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
  uint32_t lastCh = millis();
  bool manual     = lastRaw;

  if (manual) resetFullMode(); else resetBlinkMode();

  for (;;) {
    bool raw = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
    uint32_t now = millis();
    if (raw != lastRaw && (now - lastCh) > MODE_DEBOUNCE_MS) {
      lastRaw = raw; lastCh = now;
      bool newManual = raw;
      if (newManual != manual) {
        manual = newManual;
        if (manual) resetFullMode(); else resetBlinkMode();
      }
    }

    if (manual) runFullMode();
    else        runBlinkMode();

    vTaskDelay(1);
  }
}

// =================== AP + mDNS ===================
static void startApAndMdns() {
  // Modo dual (AP+STA) sin tumbar STA iniciada por WiFiManagerESP32
  WiFi.mode(WIFI_AP_STA);

  const char* AP_SSID = "config";
  const char* AP_PASS = "password";     // WPA2 (>=8 chars)
  (void)WiFi.softAP(AP_SSID, AP_PASS);

  // mDNS: http://config.local
  if (MDNS.begin("config")) {
    MDNS.addService("http", "tcp", 80);
  }

  // Si quieres evitar cualquier pulso en GPIO1/3 después del bootloader,
  // no imprimas nada más por Serial aquí.
}

// =================== setup/loop ===================
void setup() {
  // Opcional: puedes NO iniciar Serial para minimizar actividad en GPIO1/3.
  // Lo dejamos iniciado por si necesitas logs iniciales, pero no imprimimos nada luego.
  Serial.begin(SERIAL_BAUD);
  delay(100);

  // Cargar config MQTT
  cfgStore.load(cfg);

  // Iniciar Wi-Fi Manager sin usar Serial (GPIO1/3)
  static NullStream nullIO;
  wifi.begin(&nullIO, SERIAL_CLI_TIMEOUT_MS);

  // Preparar MQTT chat
  chat.setServer(cfg.host, cfg.port);
  chat.setAuth(cfg.user, cfg.pass);
  chat.setTopic(cfg.topic);
  chat.setSubTopic(cfg.subTopic);
  chat.begin();

  // Web UI
  static WebUI ui(chat, cfg, cfgStore);
  webui = &ui;
  webui->begin();
  webui->attachMqttSink();  // activa recepción (subscribe) para el chat web

  // AP “config” + mDNS “config.local”
  startApAndMdns();

  // Task de riego (core 0, prioridad baja)
  xTaskCreatePinnedToCore(irrigationTask, "irrigationTask", 6144, nullptr, 1, &gIrrigationTask, 0);

  // Si quieres cero actividad en TX0/RX0 tras el arranque:
  // Serial.end();   // <- descomenta si lo necesitas ultra-silencioso
}

void loop() {
  // Mantener servicios
  wifi.service();                   // Wi-Fi (no bloqueante, sin Serial)
  if (wifi.isConnected()) chat.loop(); // MQTT
  if (webui) webui->loop();         // HTTP

  // Opcional: detectar cambio de IP STA sin imprimir para no tocar GPIO1/3
  // (si quieres ver IPs, usa la WebUI: muestra IP AP y IP STA)

  delay(2);
}
