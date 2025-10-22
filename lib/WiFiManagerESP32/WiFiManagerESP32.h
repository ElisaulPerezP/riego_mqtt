#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

class WiFiManagerESP32 {
public:
  // API pública
  WiFiManagerESP32();
  void begin(Stream* io = &Serial, uint32_t serialTimeoutMs = 300000);
  void service();               // llama en loop() (reporta IP, etc.)
  void showMenu() const;        // imprime menú
  void handleSerial();          // CLI no bloqueante (lee 1 línea si hay)

  // Uso programático opcional
  bool connectManual(const String& ssid, const String& pass, bool openNet, bool askSaveAndAuto = true);
  bool connectSavedByIndex(int idx);
  void  listSaved() const;
  bool  setAutoIndex(int idx);
  bool  deleteSaved(int idx);
  void  disableAuto();

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  IPAddress localIP() const { return WiFi.localIP(); }
  void printConnInfo() const;

private:
  // ---- Config / estado ----
  static constexpr uint8_t  MAX_SAVED = 10;
  static constexpr uint32_t SCAN_TIMEOUT_MS    = 7000;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

  struct SavedNet { String ssid, pass; bool open; };
  SavedNet    saved_[MAX_SAVED];
  int         savedCount_ = 0;
  int         autoIdx_    = -1;

  Preferences prefs_;
  const char* NS_ = "wcmgr";

  Stream*  io_ = &Serial;
  uint32_t serialTimeoutMs_ = 300000;

  bool ipAnnounced_ = false;
  IPAddress lastIP_ = IPAddress((uint32_t)0);

  // ---- Internos ----
  static WiFiManagerESP32* self_;  // para thunk de eventos
  static void WiFiEventThunk(WiFiEvent_t event);
  void onWiFiEvent(WiFiEvent_t event);

  void loadSaved();
  void persistSaved();
  int  findSavedBySsid(const String& ssid) const;
  void addOrUpdateSaved(const String& ssid, const String& pass, bool openNet, bool setAsAuto);
  bool waitForValidIP(uint32_t timeoutMs = 7000) const;
  void serviceIPReporter();

  // CLI helpers
  void scanNetworksAndPrint(uint32_t timeout_ms = SCAN_TIMEOUT_MS, bool show_hidden = false);
  String readLine(bool echo = true, uint16_t maxLen = 128) const;
  String readPassword(uint16_t maxLen = 64) const;

  // Conexión
  bool connectNonBlocking_(const char* ssid, const char* pass, bool openNet);

  // UI
  static const char* wlStatusStr(uint8_t s);
  static String authToStr(uint8_t a);
  static String rssiBars(int32_t r);
};
