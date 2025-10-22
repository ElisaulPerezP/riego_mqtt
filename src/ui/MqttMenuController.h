#pragma once
#include <Arduino.h>
#include "core/LineReader.h"
#include "config/MqttConfig.h"
#include "config/MqttConfigStore.h"
#include "mqtt/MqttChat.h"

class MqttMenuController {
public:
  enum class State {
    Menu,
    EditHost,   // luego pide puerto
    EditPort,
    EditUser,
    EditPass,
    EditTopic,
    ChatMenu,
    ChatWaitMsg
  };

  MqttMenuController(Stream& io,
                     LineReader& reader,
                     MqttConfig& cfg,
                     MqttConfigStore& store,
                     MqttChat& chat)
  : io_(io), reader_(reader), cfg_(cfg), store_(store), chat_(chat) {}

  void enter();        // entrar al menú MQTT
  void exit();         // volver al root
  bool isActive() const { return active_; }

  // Llamar periódicamente cuando está activo
  void loop(bool wifiConnected);

private:
  void printMenu();
  void printPrompt(const char* p = "(mqtt)> ");
  void printChatMenu();
  void printChatPrompt();

  void handleLine(const String& line);

private:
  Stream&          io_;
  LineReader&      reader_;
  MqttConfig&      cfg_;
  MqttConfigStore& store_;
  MqttChat&        chat_;

  bool   active_ = false;
  State  st_     = State::Menu;
};
