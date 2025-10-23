#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <functional>

class MqttChat {
public:
  using MessageHandler = std::function<void(const String&, const String&)>;

  MqttChat(const char* host,
           uint16_t port,
           const char* user,
           const char* pass,
           const char* topic_default);

  void begin();                 // Llama en setup()
  void loop();                  // Mantiene MQTT (llamar en loop si hay WiFi)
  bool publish(const String& msg);

  // Tópicos
  void setTopic(const String& topic);
  String getTopic() const { return topic_; }

  void setSubTopic(const String& topic);   // NUEVO
  String getSubTopic() const { return subTopic_; } // NUEVO
  void subscribe();                        // NUEVO (activa suscripción)
  void unsubscribe();                      // NUEVO

  // Conexión
  bool connected() { return mqtt_.connected(); }
  void setRootCA(const char* ca_pem);      // opcional, para validar TLS
  bool ensureConnected();

  // Estado
  String status();

  // Reconfig en runtime
  void setServer(const String& host, uint16_t port);
  void setAuth(const String& user, const String& pass);

  // Handler de mensajes entrantes
  void onMessage(MessageHandler cb);       // NUEVO

private:
  String makeClientId() const;

  String      host_;
  uint16_t    port_;
  String      user_;
  String      pass_;
  String      topic_;

  // NUEVO
  String          subTopic_;
  bool            subActive_   = false;  // ¿debo estar suscrito?
  bool            subscribed_  = false;  // ¿ya hice subscribe() con éxito?
  MessageHandler  handler_;

  const char*    root_ca_pem_ = nullptr;

  WiFiClientSecure tls_;
  PubSubClient     mqtt_;
  String           clientId_;
};
