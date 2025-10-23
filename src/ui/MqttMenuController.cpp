#include "MqttMenuController.h"

void MqttMenuController::enter() {
  active_ = true;
  st_ = State::Menu;
  io_.println();
  printMenu();
  printPrompt();
}

void MqttMenuController::exit() {
  active_ = false;
}

void MqttMenuController::loop(bool wifiConnected) {
  if (!active_) return;

  if (wifiConnected) chat_.loop();

  String line;
  if (!reader_.poll(line)) return;

  String trimmed = line;
  trimmed.trim();
  handleLine(trimmed);
}

void MqttMenuController::printMenu() {
  io_.println(F("===== MQTT MENU ====="));
  io_.println(F("[h] Host/puerto"));
  io_.println(F("[u] Usuario"));
  io_.println(F("[p] Contraseña"));
  io_.println(F("[t] Tópico (publicación)"));
  io_.println(F("[l] Tópico suscripción"));   // NUEVO
  io_.println(F("[c] Chat MQTT"));
  io_.println(F("[s] Estado"));
  io_.println(F("[e] Volver al menú Wi-Fi"));
  io_.println(F("---------------------"));
  io_.printf("broker: %s:%u  user: %s  pub: %s  sub: %s\n",
             cfg_.host.c_str(), cfg_.port, cfg_.user.c_str(),
             cfg_.topic.c_str(), cfg_.subTopic.c_str());
}

void MqttMenuController::printPrompt(const char* p) {
  io_.print(F(""));
  io_.print(p);
}

void MqttMenuController::printChatMenu() {
  io_.println();
  io_.println(F("----- CHAT MQTT -----"));
  io_.println(F("Comandos:"));
  io_.println(F("  /m        -> pedir mensaje y publicar"));
  io_.println(F("  /s        -> estado MQTT/WiFi/tópicos"));
  io_.println(F("  /e        -> salir al menú MQTT"));
  io_.println(F("  (cualquier otra línea se publica tal cual)"));
  io_.print  (F("Topic (pub): ")); io_.println(cfg_.topic);
  io_.print  (F("Topic (sub): ")); io_.println(cfg_.subTopic);
  io_.println(F("---------------------"));
}

void MqttMenuController::printChatPrompt() {
  io_.print(F("(chat)> "));
}

void MqttMenuController::handleLine(const String& line) {
  // ===== Estados de edición con /k =====
  switch (st_) {
    case State::EditHost: {
      if (line != "/k" && line.length()) cfg_.host = line;
      chat_.setServer(cfg_.host, cfg_.port);
      store_.save(cfg_);
      io_.printf("Puerto actual: %u\n", cfg_.port);
      io_.print  (F("Nuevo puerto (1-65535) o /k para mantener: "));
      st_ = State::EditPort;
      return;
    }
    case State::EditPort: {
      if (line != "/k" && line.length()) {
        long p = line.toInt();
        if (p >= 1 && p <= 65535) cfg_.port = (uint16_t)p;
        else io_.println(F("[mqtt] puerto inválido, se mantiene el actual"));
      }
      chat_.setServer(cfg_.host, cfg_.port);
      store_.save(cfg_);
      io_.printf("[mqtt] broker -> %s:%u\n", cfg_.host.c_str(), cfg_.port);
      st_ = State::Menu;
      printPrompt();
      return;
    }
    case State::EditUser: {
      if (line != "/k" && line.length()) cfg_.user = line;
      chat_.setAuth(cfg_.user, cfg_.pass);
      store_.save(cfg_);
      io_.printf("[mqtt] user -> %s\n", cfg_.user.c_str());
      st_ = State::Menu;
      printPrompt();
      return;
    }
    case State::EditPass: {
      if (line != "/k" && line.length()) cfg_.pass = line;
      chat_.setAuth(cfg_.user, cfg_.pass);
      store_.save(cfg_);
      io_.println(F("[mqtt] pass -> (actualizada)"));
      st_ = State::Menu;
      printPrompt();
      return;
    }
    case State::EditTopic: {
      if (line != "/k" && line.length()) cfg_.topic = line;
      chat_.setTopic(cfg_.topic);
      store_.save(cfg_);
      io_.printf("[mqtt] topic(pub) -> %s\n", cfg_.topic.c_str());
      st_ = State::Menu;
      printPrompt();
      return;
    }
    case State::EditSubTopic: {   // NUEVO
      if (line != "/k" && line.length()) cfg_.subTopic = line;
      chat_.setSubTopic(cfg_.subTopic);
      store_.save(cfg_);
      io_.printf("[mqtt] topic(sub) -> %s\n", cfg_.subTopic.c_str());
      st_ = State::Menu;
      printPrompt();
      return;
    }
    case State::ChatWaitMsg: {
      if (!line.length()) {
        st_ = State::ChatMenu;
        printChatMenu();
        printChatPrompt();
        return;
      }
      if (WiFi.isConnected() && chat_.publish(line)) {
        io_.print(F("[mqtt-> ")); io_.print(cfg_.topic); io_.print(F("] "));
        io_.println(line);
      } else {
        io_.println(F("[chat] no publicado: MQTT/Wi-Fi no conectado"));
      }
      st_ = State::ChatMenu;
      printChatPrompt();
      return;
    }
    default: break;
  }

  // ===== Menú principal MQTT =====
  if (st_ == State::Menu) {
    if (!line.length()) {
      io_.println();
      printMenu();
      printPrompt();
      return;
    }
    if (line.length() == 1) {
      switch (line[0]) {
        case 'h': case 'H':
          io_.printf("Host actual: %s\n", cfg_.host.c_str());
          io_.print  (F("Nuevo host o /k para mantener: "));
          st_ = State::EditHost;
          return;
        case 'u': case 'U':
          io_.printf("Usuario actual: %s\n", cfg_.user.c_str());
          io_.print  (F("Nuevo usuario o /k para mantener: "));
          st_ = State::EditUser;
          return;
        case 'p': case 'P':
          io_.println(F("Introduce nueva contraseña o /k para mantener:"));
          st_ = State::EditPass;
          return;
        case 't': case 'T':
          io_.printf("Tópico (pub) actual: %s\n", cfg_.topic.c_str());
          io_.print  (F("Nuevo tópico o /k para mantener: "));
          st_ = State::EditTopic;
          return;
        case 'l': case 'L': // NUEVO
          io_.printf("Tópico (sub) actual: %s\n", cfg_.subTopic.c_str());
          io_.print  (F("Nuevo tópico o /k para mantener: "));
          st_ = State::EditSubTopic;
          return;
        case 's': case 'S':
          io_.println(chat_.status());
          printPrompt();
          return;
        case 'c': case 'C':
          io_.println();
          st_ = State::ChatMenu;

          // Al entrar al chat: engancha handler y suscríbete
          chat_.onMessage([this](const String& topic, const String& payload){
            io_.print(F("[mqtt<- ")); io_.print(topic); io_.print(F("] "));
            io_.println(payload);
          });
          chat_.setSubTopic(cfg_.subTopic);
          chat_.subscribe();

          printChatMenu();
          printChatPrompt();
          return;
        case 'e': case 'E':
          exit();
          return;
      }
    }
    // No reconocido -> prompt
    printPrompt();
    return;
  }

  // ===== Menú de chat (slash commands) =====
  if (st_ == State::ChatMenu) {
    if (!line.length()) {
      printChatMenu();
      return;
    }
    if (line == "/m" || line == "/M") {
      io_.print(F("Mensaje: "));
      st_ = State::ChatWaitMsg;
      return;
    }
    if (line == "/s" || line == "/S") {
      io_.println(chat_.status());
      printChatPrompt();
      return;
    }
    if (line == "/e" || line == "/E") {
      // Al salir del chat, desuscribirse
      chat_.unsubscribe();
      st_ = State::Menu;
      printPrompt();
      return;
    }

    // Publicación directa
    if (line.length()) {
      if (WiFi.isConnected() && chat_.publish(line)) {
        io_.print(F("[mqtt-> ")); io_.print(cfg_.topic); io_.print(F("] "));
        io_.println(line);
      } else {
        io_.println(F("[chat] no publicado: MQTT/Wi-Fi no conectado"));
      }
    }
    printChatPrompt();
    return;
  }
}
