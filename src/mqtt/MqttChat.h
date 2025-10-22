#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

class MqttChat {
public:
  MqttChat(const char* host,
           uint16_t port,
           const char* user,
           const char* pass,
           const char* topic_default);

  void begin();                 // Llama en setup()
  void loop();                  // Mantiene MQTT (llamar en loop si hay WiFi)
  bool publish(const String& msg);

  // Tópico
  void setTopic(const String& topic);
  String getTopic() const { return topic_; }

  // Conexión
  bool connected() { return mqtt_.connected(); }  // PubSubClient::connected() no es const
  void setRootCA(const char* ca_pem);             // opcional, para validar TLS
  bool ensureConnected();

  // Estado
  String status();

  // Reconfig en runtime
  void setServer(const String& host, uint16_t port);
  void setAuth(const String& user, const String& pass);

private:
  String makeClientId() const;

  String      host_;
  uint16_t    port_;
  String      user_;
  String      pass_;
  String      topic_;

  const char* root_ca_pem_ = nullptr;

  WiFiClientSecure tls_;
  PubSubClient     mqtt_;
  String           clientId_;
};
