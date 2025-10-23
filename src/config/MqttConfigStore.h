#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "MqttConfig.h"

class MqttConfigStore {
public:
  explicit MqttConfigStore(const char* ns = "mqtt") : ns_(ns) {}

  void load(MqttConfig& cfg) {
    if (!prefs_.begin(ns_, /*readOnly=*/true)) return;

    cfg.host     = prefs_.getString("host",  cfg.host);
    cfg.port     = prefs_.getUShort("port",  cfg.port);
    cfg.user     = prefs_.getString("user",  cfg.user);
    cfg.pass     = prefs_.getString("pass",  cfg.pass);
    cfg.topic    = prefs_.getString("topic", cfg.topic);     // publicación
    cfg.subTopic = prefs_.getString("sub",   cfg.subTopic);  // suscripción (NUEVO)

    prefs_.end();
  }

  void save(const MqttConfig& cfg) {
    if (!prefs_.begin(ns_, /*readOnly=*/false)) return;

    prefs_.putString("host",  cfg.host);
    prefs_.putUShort("port",  cfg.port);
    prefs_.putString("user",  cfg.user);
    prefs_.putString("pass",  cfg.pass);
    prefs_.putString("topic", cfg.topic);     // publicación
    prefs_.putString("sub",   cfg.subTopic);  // suscripción (NUEVO)

    prefs_.end();
  }

private:
  const char* ns_;
  Preferences prefs_;
};
