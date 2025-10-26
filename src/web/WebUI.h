// File: src/web/WebUI.h
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <functional>
#include <utility>
#include <vector>

#include "../mqtt/MqttChat.h"
#include "../config/MqttConfig.h"
#include "../config/MqttConfigStore.h"
#include "../modes/AutoMode.h"          // Define StartSpec / StepSpec
#include "../state/RelayState.h"        // Define RelayState

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
  void attachScheduleAPI(
    std::function<bool()>                              getProgramEnabled,
    std::function<void(bool)>                          setProgramEnabled,
    std::function<std::vector<StartSpec>()>            getStarts,
    std::function<bool(const StartSpec&)>              addStart,
    std::function<bool(unsigned)>                      deleteStart,
    std::function<std::vector<StepSpec>()>             getSteps,
    std::function<bool(const std::vector<StepSpec>&)>  setSteps,
    std::function<String()>                            nowStrProvider,
    std::function<bool(const std::vector<StartSpec>&)> setStartsBulk
  ) {
    getProgramEnabled_ = getProgramEnabled;
    setProgramEnabled_ = setProgramEnabled;
    getStarts_         = getStarts;
    addStart_          = addStart;
    deleteStart_       = deleteStart;
    getSteps_          = getSteps;
    setSteps_          = setSteps;
    nowStrProvider_    = nowStrProvider;
    setStartsBulk_     = setStartsBulk;
  }

  // ======== API de ESTADOS ========
  void attachStateAPI(
    std::function<std::vector<RelayState>()> getter,
    std::function<bool(const std::vector<RelayState>&)> setter,
    std::function<std::pair<int,int>()> counts,
    std::function<String(int,bool)> relayNameGetter = nullptr
  ) {
    getStates_       = getter;
    setStates_       = setter;
    getCounts_       = counts;
    relayNameGetter_ = relayNameGetter;
  }

  // ----------------- Estructuras públicas útiles -----------------
  struct ZoneParams {
    uint32_t volumeMl = 0;
    uint32_t timeMs   = 0;
    uint8_t  fert1Pct = 0;
    uint8_t  fert2Pct = 0;
  };

private:
  // ---------- Wi-Fi guardadas ----------
  struct SavedNet {
    String ssid; String pass; bool open;
    SavedNet() : open(false) {}
    SavedNet(const String& s, const String& p, bool o) : ssid(s), pass(p), open(o) {}
  };

  // Namespaces de Preferences
  static constexpr const char* NS_WIFI  = "wifi_saved";
  static constexpr const char* NS_ZONES = "zones";

  // Máx redes guardadas
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

  // ---------- Persistencia de parámetros por zona ----------
  bool loadZoneParams(int idx, ZoneParams& out);
  bool saveZoneParams(int idx, const ZoneParams& z);
  bool deleteZoneParams(int idx);
  int  getZonesCount();
  void setZonesCount(int count);
  void compactZonesAfterDelete(int deletedIdx, int newCount);

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

  // Riego / Telemetría (placeholder)
  void handleIrrigation();
  void handleIrrigationJson();

  // ESTADOS (tabla)
  void handleStatesList();
  void handleStatesSave();
  void handleStatesDelete();

  // DETALLE DE ZONA
  void handleStateEdit();
  void handleStateEditSave();

  // ======= Programación (/sched) =======
  void handleSched();                 // vista principal de programación
  void handleSchedEnable();           // activar/desactivar programa
  void handleSchedAddStart();         // añadir un horario
  void handleSchedDeleteStart();      // eliminar un horario
  void handleSchedSaveStarts();       // guardar tabla completa (bulk)

  // Pasos (/sched/steps)
  void handleStepsPage();             // vista de pasos (StepSet 0)
  void handleStepsSave();             // guardar pasos

  // ======= Modo =======
  void handleMode();
  void handleModeSet();
  void handleModeManualStart();   // <-- NUEVO (declarado)
  void handleModeManualStop();    // <-- NUEVO (declarado)

private:
  WebServer&        server_;
  MqttChat&         chat_;
  MqttConfig&       cfg_;
  MqttConfigStore&  cfgStore_;

  // Schedule API
  std::function<bool()>                              getProgramEnabled_;
  std::function<void(bool)>                          setProgramEnabled_;
  std::function<std::vector<StartSpec>()>            getStarts_;
  std::function<bool(const StartSpec&)>              addStart_;
  std::function<bool(unsigned)>                      deleteStart_;
  std::function<std::vector<StepSpec>()>             getSteps_;
  std::function<bool(const std::vector<StepSpec>&)>  setSteps_;
  std::function<String()>                            nowStrProvider_;
  std::function<bool(const std::vector<StartSpec>&)> setStartsBulk_;

  // States API
  std::function<std::vector<RelayState>()>             getStates_;
  std::function<bool(const std::vector<RelayState>&)>  setStates_;
  std::function<std::pair<int,int>()>                  getCounts_;
  std::function<String(int,bool)>                      relayNameGetter_;
};
