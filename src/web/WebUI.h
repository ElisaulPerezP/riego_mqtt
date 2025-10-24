// File: src/web/WebUI.h
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <functional>
#include <vector>

#include "../mqtt/MqttChat.h"
#include "../config/MqttConfig.h"
#include "../config/MqttConfigStore.h"
#include "../modes/AutoMode.h"
#include "../state/RelayState.h"

class WebUI {
public:
  WebUI(WebServer& server,
        MqttChat& chat,
        MqttConfig& cfg,
        MqttConfigStore& store)
  : server_(server), chat_(chat), cfg_(cfg), cfgStore_(store) {}

  void begin();
  void loop();

  void attachMqttSink();

  // ======== API de Schedule ========
  // Incluye setStarts (actualización masiva de starts).
  void attachScheduleAPI(
    std::function<bool()>                               getProgramEnabled,
    std::function<void(bool)>                           setProgramEnabled,
    std::function<std::vector<StartSpec>()>             getStarts,
    std::function<bool(const StartSpec&)>               addStart,
    std::function<bool(unsigned)>                       deleteStart,
    std::function<std::vector<StepSpec>()>              getSteps,
    std::function<bool(const std::vector<StepSpec>&)>   setSteps,
    std::function<String()>                              nowStrProvider,
    std::function<bool(const std::vector<StartSpec>&)>   setStarts
  ) {
    getProgramEnabled_ = getProgramEnabled;
    setProgramEnabled_ = setProgramEnabled;
    getStarts_         = getStarts;
    addStart_          = addStart;
    deleteStart_       = deleteStart;
    getSteps_          = getSteps;
    setSteps_          = setSteps;
    nowStrProvider_    = nowStrProvider;
    setStarts_         = setStarts;
  }

  // ======== API de ESTADOS (tabla de relés) ========
  void attachStateAPI(
    std::function<std::vector<RelayState>()> getter,
    std::function<bool(const std::vector<RelayState>&)> setter,
    std::function<std::pair<int,int>()> counts,
    std::function<String(int,bool)> relayNameGetter = nullptr
  ) {
    getStates_ = getter;
    setStates_ = setter;
    getCounts_ = counts;
    relayNameGetter_ = relayNameGetter;
  }

private:
  // ---------- Wi-Fi guardadas ----------
  struct SavedNet {
    String ssid; String pass; bool open;
    SavedNet() : open(false) {}
    SavedNet(const String& s, const String& p, bool o) : ssid(s), pass(p), open(o) {}
  };
  static constexpr const char* NS_WIFI = "wifi_saved";
  static constexpr int MAX_SAVED = 10;
  SavedNet saved_[MAX_SAVED];
  int savedCount_ = 0;
  int autoIdx_    = -1;

  bool loadSaved();
  bool persistSaved();
  int  findSavedBySsid(const String& ssid) const;
  bool addOrUpdateSaved(const String& ssid, const String& pass, bool openNet, bool setAuto);
  bool deleteSaved(int idx);
  bool setAutoIndex(int idx);

  // ---------- Persistencia de Zona (por índice de estado) ----------
  struct ZoneParams {
    uint32_t volumeMl = 0;
    uint32_t timeMs   = 0;
    uint8_t  fert1Pct = 0;   // 0..100
    uint8_t  fert2Pct = 0;   // 0..100
  };
  static constexpr const char* NS_ZONES = "zones";

  bool loadZoneParams(int idx, ZoneParams& out);
  bool saveZoneParams(int idx, const ZoneParams& z);
  bool deleteZoneParams(int idx);
  int  getZonesCount();
  void setZonesCount(int count);
  void compactZonesAfterDelete(int deletedIdx, int newCount); // mueve idx+1..N-1 hacia arriba

  // ---------- MQTT sink buffer ----------
  struct Msg {
    String topic; String payload; unsigned long ms=0; unsigned long seq=0;
  };
  static constexpr size_t MSG_BUF = 40;
  Msg     inbox_[MSG_BUF];
  size_t  writePos_ = 0;
  size_t  used_     = 0;
  unsigned long seqCounter_ = 0;

  void pushMsg_(const String& t, const String& p);

  // ---------- HTML helpers ----------
  String htmlHeader(const String& title) const;
  String htmlFooter() const;
  String jsonEscape(String s);

  // ---------- HTTP handlers ----------
  void handleRoot();
  void handleWifiInfo();
  void handleWifiScan();
  void handleWifiConnect();
  void handleWifiSaved();
  void handleWifiSavedAction();

  void handleMqtt();
  void handleMqttSet();
  void handleMqttPublish();
  void handleMqttPoll();

  // Irrigación / Telemetría (placeholder)
  void handleIrrigation();
  void handleIrrigationJson();

  // ======== ESTADOS (tabla principal) ========
  void handleStatesList();
  void handleStatesSave();
  void handleStatesDelete();

  // ======== DETALLE DE ZONA ========
  void handleStateEdit();       // GET /states/edit?idx=N
  void handleStateEditSave();   // POST /states/edit/save

private:
  WebServer&        server_;
  MqttChat&         chat_;
  MqttConfig&       cfg_;
  MqttConfigStore&  cfgStore_;

  // Schedule API
  std::function<bool()>                                getProgramEnabled_;
  std::function<void(bool)>                            setProgramEnabled_;
  std::function<std::vector<StartSpec>()>              getStarts_;
  std::function<bool(const StartSpec&)>                addStart_;
  std::function<bool(unsigned)>                        deleteStart_;
  std::function<std::vector<StepSpec>()>               getSteps_;
  std::function<bool(const std::vector<StepSpec>&)>    setSteps_;
  std::function<String()>                              nowStrProvider_;
  std::function<bool(const std::vector<StartSpec>&)>   setStarts_;

  // States API
  std::function<std::vector<RelayState>()>             getStates_;
  std::function<bool(const std::vector<RelayState>&)>  setStates_;
  std::function<std::pair<int,int>()>                  getCounts_;
  std::function<String(int,bool)>                      relayNameGetter_;
};
