// File: src/web/WebUI_Routes.cpp
#include "web/WebUI.h"

void WebUI::begin() {
  // Home + Wi-Fi
  server_.on("/",               HTTP_GET,  [this]{ handleRoot(); });
  server_.on("/wifi/info",      HTTP_GET,  [this]{ handleWifiInfo(); });
  server_.on("/wifi/scan",      HTTP_GET,  [this]{ handleWifiScan(); });
  server_.on("/wifi/connect",   HTTP_POST, [this]{ handleWifiConnect(); });
  server_.on("/wifi/saved",     HTTP_GET,  [this]{ handleWifiSaved(); });
  server_.on("/wifi/saved/do",  HTTP_POST, [this]{ handleWifiSavedAction(); });

  // MQTT
  server_.on("/mqtt",           HTTP_GET,  [this]{ handleMqtt(); });
  server_.on("/mqtt/set",       HTTP_POST, [this]{ handleMqttSet(); });
  server_.on("/mqtt/publish",   HTTP_POST, [this]{ handleMqttPublish(); });
  server_.on("/mqtt/poll",      HTTP_GET,  [this]{ handleMqttPoll(); });

  // Modo
  server_.on("/mode",           HTTP_GET,  [this]{ handleMode(); });
  server_.on("/mode/set",       HTTP_POST, [this]{ handleModeSet(); });

  // Riego (placeholder)
  server_.on("/riego",          HTTP_GET,  [this]{ handleIrrigation(); });
  server_.on("/riego.json",     HTTP_GET,  [this]{ handleIrrigationJson(); });

  // Estados
  server_.on("/states",         HTTP_GET,  [this]{ handleStatesList(); });
  server_.on("/states/save",    HTTP_POST, [this]{ handleStatesSave(); });
  server_.on("/states/delete",  HTTP_POST, [this]{ handleStatesDelete(); });

  // Detalle zona
  server_.on("/states/edit",        HTTP_GET,  [this]{ handleStateEdit();      });
  server_.on("/states/edit/save",   HTTP_POST, [this]{ handleStateEditSave();  });

  server_.onNotFound([this]{ server_.send(404, F("text/plain"), F("404")); });
  server_.begin();
}

void WebUI::loop() { server_.handleClient(); }

void WebUI::attachMqttSink() {
  chat_.onMessage([this](const String& t, const String& p){ pushMsg_(t, p); });
  chat_.subscribe();
}
