#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config/MqttConfig.h"
#include "config/MqttConfigStore.h"
#include "mqtt/MqttChat.h"

class WebUI {
public:
  WebUI(MqttChat& chat, MqttConfig& cfg, MqttConfigStore& store)
  : chat_(chat), cfg_(cfg), cfgStore_(store) {}

  void begin();
  void loop();

  // Llamar una vez desde setup()
  void attachMqttSink();

private:
  // ===== Wi-Fi guardadas (idéntico a WiFiManagerESP32) =====
  struct SavedNet { String ssid, pass; bool open; };
  static constexpr uint8_t MAX_SAVED = 10;
  const char* NS_WIFI = "wcmgr";

  bool loadSaved();
  bool persistSaved();
  int  findSavedBySsid(const String& ssid) const;
  bool addOrUpdateSaved(const String& ssid, const String& pass, bool openNet, bool setAuto);
  bool deleteSaved(int idx);
  bool setAutoIndex(int idx);

  // ===== HTTP handlers =====
  void handleRoot();
  void handleWifiInfo();
  void handleWifiScan();
  void handleWifiConnect();      // POST ssid, pass, open, save
  void handleWifiSaved();        // lista + acciones
  void handleWifiSavedAction();  // POST action=connect|setauto|delete|disableauto idx=#

  void handleMqtt();
  void handleMqttSet();          // POST host,port,user,pass,topic,sub
  void handleMqttPublish();      // POST msg
  void handleMqttPoll();         // JSON de mensajes recientes

  void handleNotFound();

  // ===== Helpers HTML/JSON =====
  String htmlHeader(const String& title) const;
  String htmlFooter() const;
  static String jsonEscape(String s);

  // ===== Buffer de mensajes MQTT (para /mqtt/poll) =====
  struct Msg { String topic, payload; unsigned long ms; unsigned long seq; };
  static constexpr size_t MSG_BUF = 50;
  Msg    inbox_[MSG_BUF];
  size_t writePos_ = 0;           // índice circular de escritura
  size_t used_     = 0;           // ocupación real
  unsigned long seqCounter_ = 0;  // SIEMPRE creciente (monótono)

  void pushMsg_(const String& t, const String& p);

private:
  WebServer server_{80};
  Preferences prefs_;

  // estado Wi-Fi guardado
  SavedNet saved_[MAX_SAVED];
  int savedCount_ = 0;
  int autoIdx_    = -1;

  // refs
  MqttChat&         chat_;
  MqttConfig&       cfg_;
  MqttConfigStore&  cfgStore_;
};
