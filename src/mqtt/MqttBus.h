#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <functional>

class MqttBus {
public:
  using MessageHandler = std::function<void(const String& topic, const String& payload)>;

  MqttBus(const String& host, uint16_t port,
          const String& user, const String& pass);

  void setServer(const String& host, uint16_t port);
  void setAuth(const String& user, const String& pass);
  void setRootCA(const char* ca_pem); // opcional
  void begin();
  void loop();

  bool connected();
  bool ensureConnected();

  void subscribe(const String& topic);
  void onMessage(MessageHandler cb);

  bool publish(const String& topic, const String& payload, bool retained=false);

private:
  String makeClientId() const;

  String host_; uint16_t port_;
  String user_; String pass_;
  const char* root_ca_ = nullptr;

  WiFiClientSecure tls_;
  PubSubClient     mqtt_;
  MessageHandler   handler_;
};
