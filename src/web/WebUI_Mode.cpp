// File: src/web/WebUI_Mode.cpp
#include "web/WebUI.h"
#include <Preferences.h>

static const char* NS_MODE = "mode";

void WebUI::handleMode() {
  Preferences p;
  bool ovr = false, manual = false;
  if (p.begin(NS_MODE, /*ro*/ true)) {
    ovr    = p.getUChar("ovr", 0) != 0;
    manual = p.getUChar("manual", 0) != 0;
    p.end();
  }

  String s = htmlHeader(F("Modo"));
  s += F("<h3>Modo de operación</h3>");
  s += F("<p>Si activas el <b>override por software</b>, el equipo ignorará el interruptor físico y usará el modo elegido aquí.</p>");
  s += F("<form class='formcard' method='post' action='/mode/set'>");
  s += F("<p><label><input type='checkbox' name='ovr' "); if (ovr) s += F("checked"); s += F("> Activar override por software</label></p>");
  s += F("<p>Modo con override:&nbsp; "
         "<label><input type='radio' name='mode' value='auto' "); if (!manual) s += F("checked"); s += F("> Automático</label>&nbsp; "
         "<label><input type='radio' name='mode' value='manual' "); if (manual) s += F("checked"); s += F("> Manual</label></p>");
  s += F("<p><button class='btn'>Guardar</button></p></form>");
  s += htmlFooter();

  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleModeSet() {
  bool ovr = server_.hasArg("ovr");
  bool manual = server_.hasArg("mode") && server_.arg("mode") == "manual";

  Preferences p;
  if (p.begin(NS_MODE, /*ro*/ false)) {
    p.putUChar("ovr",    ovr ? 1 : 0);
    p.putUChar("manual", manual ? 1 : 0);
    p.end();
  }
  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}
