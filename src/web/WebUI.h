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
#include "../schedule/IrrigationSchedule.h"  // StartSpec / StepSpec
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
  // Firma extendida: añadimos setStarts (opcional) para actualizar filas de horarios completas.
  void attachScheduleAPI(
    std::function<bool()>                           getProgramEnabled,
    std::function<void(bool)>                       setProgramEnabled,
    std::function<std::vector<StartSpec>()>         getStarts,
    std::function<bool(const StartSpec&)>           addStart,
    std::function<bool(unsigned)>                   deleteStart,
    std::function<std::vector<StepSpec>()>          getSteps,
    std::function<bool(const std::vector<StepSpec>&)> setSteps,
    std::function<String()>                         nowStrProvider,
    std::function<bool(const std::vector<StartSpec>&)> setStarts = nullptr
  ) {
    getProgramEnabled_ = std::move(getProgramEnabled);
    setProgramEnabled_ = std::move(setProgramEnabled);
    getStarts_         = std::move(getStarts);
    addStart_          = std::move(addStart);
    deleteStart_       = std::move(deleteStart);
    getSteps_          = std::move(getSteps);
    setSteps_          = std::move(setSteps);
    nowStrProvider_    = std::move(nowStrProvider);
    setStarts_         = std::move(setStarts);
  }

  // ======== API de ESTADOS (nuevo) ========
  // counts: devuelve {numMains, numSecs}
  // relayNameGetter: opcional => nombre de columna por índice (para mains y secs: main=true/false)
  void attachStateAPI(
    std::function<std::vector<RelayState>()> getter,
    std::function<bool(const std::vector<RelayState>&)> setter,
    std::function<std::pair<int,int>()> counts,
    std::function<String(int,bool)> relayNameGetter = nullptr
  ) {
    getStates_ = std::move(getter);
    setStates_ = std::move(setter);
    getCounts_ = std::move(counts);
    relayNameGetter_ = std::move(relayNameGetter);
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

  // Irrigación / Telemetría (si ya lo usas)
  void handleIrrigation();
  void handleIrrigationJson();

  // ======== ESTADOS (nuevo) ========
  void handleStatesList();
  void handleStatesSave();    // crea/actualiza (idx=-1 => nuevo)
  void handleStatesDelete();  // elimina

private:
  WebServer&        server_;
  MqttChat&         chat_;
  MqttConfig&       cfg_;
  MqttConfigStore&  cfgStore_;

  // Schedule API
  std::function<bool()>                            getProgramEnabled_;
  std::function<void(bool)>                        setProgramEnabled_;
  std::function<std::vector<StartSpec>()>          getStarts_;
  std::function<bool(const StartSpec&)>            addStart_;
  std::function<bool(unsigned)>                    deleteStart_;
  std::function<std::vector<StepSpec>()>           getSteps_;
  std::function<bool(const std::vector<StepSpec>&)> setSteps_;
  std::function<String()>                          nowStrProvider_;
  std::function<bool(const std::vector<StartSpec>&)> setStarts_; // opcional

  // States API
  std::function<std::vector<RelayState>()>             getStates_;
  std::function<bool(const std::vector<RelayState>&)>  setStates_;
  std::function<std::pair<int,int>()>                  getCounts_;
  std::function<String(int,bool)>                      relayNameGetter_;
};
