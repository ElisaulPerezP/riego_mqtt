#pragma once
#include <Arduino.h>

struct MqttConfig {
  String host  = "mqtt.arandanosdemipueblo.online";
  uint16_t port= 8883;
  String user  = "Elisaul";
  String pass  = "Elisaulp";
  String topic = "public/chat";
};
