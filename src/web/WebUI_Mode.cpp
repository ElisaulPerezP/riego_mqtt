// File: src/web/WebUI_Mode.cpp
#include "web/WebUI.h"
#include <Preferences.h>

// Namespace de persistencia para la página de Modo
static const char* NS_MODE = "mode";

/*
 Claves usadas en NS_MODE:
   - "ovr"     : UChar 0/1  -> override por software activo
   - "manual"  : UChar 0/1  -> modo manual (si ovr=1)
   - "sel"     : Int        -> índice de estado seleccionado (para manual)
   - "p1"      : UChar 0-100 -> porcentaje fertilizante 1
   - "p2"      : UChar 0-100 -> porcentaje fertilizante 2
   - "run"     : UChar 0/1   -> si hay ejecución manual en curso
   - "run_since": ULong      -> millis() del inicio de la ejecución manual
*/

// =============== Vista principal: /mode =================
void WebUI::handleMode() {
  // Lee preferencias actuales
  bool    ovr    = false;
  bool    manual = false;
  bool    running= false;
  int     sel    = -1;
  uint8_t p1     = 0;
  uint8_t p2     = 0;

  {
    Preferences p;
    if (p.begin(NS_MODE, /*ro*/ true)) {
      ovr     = p.getUChar("ovr", 0) != 0;
      manual  = p.getUChar("manual", 0) != 0;
      running = p.getUChar("run", 0) != 0;
      sel     = p.getInt("sel", -1);
      p1      = p.getUChar("p1", 0);
      p2      = p.getUChar("p2", 0);
      p.end();
    }
  }

  // Estados disponibles (para la UI en Manual)
  std::vector<RelayState> states;
  if (getStates_) {
    states = getStates_();
  }

  String s = htmlHeader(F("Modo"));
  s += F("<h3>Modo de operación</h3>");
  s += F("<p>Si activas el <b>override por software</b>, el equipo ignora el interruptor físico "
         "y usa el modo elegido aquí.</p>");

  // ---- Form principal: override + selección de modo ----
  s += F("<form class='formcard' method='post' action='/mode/set'>");
  s += F("<p><label><input type='checkbox' name='ovr' ");
  if (ovr) s += F("checked");
  s += F("> Activar override por software</label></p>");

  s += F("<p>Modo con override:&nbsp; ");
  s += F("<label><input type='radio' name='mode' value='auto' ");
  if (!manual) s += F("checked");
  s += F("> Automático</label>&nbsp; ");
  s += F("<label><input type='radio' name='mode' value='manual' ");
  if (manual) s += F("checked");
  s += F("> Manual</label></p>");

  s += F("<p><button class='btn'>Guardar</button></p>");
  s += F("</form>");

  // ---- UI Manual si procede (no toca override al iniciar/parar) ----
  if (ovr && manual) {
    s += F("<div class='formcard'>");
    s += F("<h4>Control manual</h4>");

    // Selección de estado/zona
    s += F("<form method='post' action='/mode/manual/start'>");
    s += F("<p><label>Estado/Zona:&nbsp;<select name='sel'>");

    for (size_t i = 0; i < states.size(); ++i) {
      s += F("<option value='");
      s += String((int)i);
      s += F("'");
      if ((int)i == sel) s += F(" selected");
      s += F(">");
      // Nombre visible
      if (states[i].name.length()) {
        s += states[i].name;
      } else {
        s += F("Estado ");
        s += String((int)i);
      }
      s += F("</option>");
    }
    s += F("</select></label></p>");

    // Porcentajes fertilizantes
    s += F("<p>p_fert_1:&nbsp;"
           "<input type='range' id='p1r' name='p1' min='0' max='100' value='");
    s += String(p1);
    s += F("' oninput='p1v.value=this.value'> "
           "<input type='number' id='p1v' min='0' max='100' value='");
    s += String(p1);
    s += F("' oninput='p1r.value=this.value'> %</p>");

    s += F("<p>p_fert_2:&nbsp;"
           "<input type='range' id='p2r' name='p2' min='0' max='100' value='");
    s += String(p2);
    s += F("' oninput='p2v.value=this.value'> "
           "<input type='number' id='p2v' min='0' max='100' value='");
    s += String(p2);
    s += F("' oninput='p2r.value=this.value'> %</p>");

    s += F("<p><button class='btn'>Iniciar</button></p>");
    s += F("</form>");

    // Si está corriendo, botón Parar (sin tocar ovr/manual)
    if (running) {
      s += F("<form method='post' action='/mode/manual/stop'>");
      s += F("<p><button class='btn'>Parar</button></p>");
      s += F("</form>");
    }

    s += F("</div>");
  }

  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

// =============== POST: /mode/set =================
// IMPORTANTE: aquí solo se escriben 'ovr' y 'manual'. NO se limpian otras claves.
void WebUI::handleModeSet() {
  bool ovr    = server_.hasArg("ovr");
  bool manual = (server_.hasArg("mode") && server_.arg("mode") == "manual");

  Preferences p;
  if (p.begin(NS_MODE, /*ro*/ false)) {
    // No p.clear(): preserva sel/p1/p2/run/etc.
    p.putUChar("ovr",    ovr ? 1 : 0);
    p.putUChar("manual", manual ? 1 : 0);
    p.end();
  }

  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}

// =============== POST: /mode/manual/start =================
// IMPORTANTE: NO tocar 'ovr' ni 'manual'. Solo el "estado de la corrida" manual.
void WebUI::handleModeManualStart() {
  int sel = server_.hasArg("sel") ? server_.arg("sel").toInt() : -1;
  uint8_t p1 = server_.hasArg("p1") ? (uint8_t)constrain(server_.arg("p1").toInt(), 0, 100) : 0;
  uint8_t p2 = server_.hasArg("p2") ? (uint8_t)constrain(server_.arg("p2").toInt(), 0, 100) : 0;

  Preferences p;
  if (p.begin(NS_MODE, /*ro*/ false)) {
    // Preserva ovr/manual: NO p.clear()
    p.putInt   ("sel", sel);
    p.putUChar ("p1",  p1);
    p.putUChar ("p2",  p2);
    p.putUChar ("run", 1);               // marca que hay ejecución manual
    p.putULong ("run_since", millis());  // por si otra parte del sistema mide volumen/tiempo
    p.end();
  }

  // Activación real de la zona debe hacerse en tu lógica de control leyendo estas claves.
  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}

// =============== POST: /mode/manual/stop =================
// IMPORTANTE: NO tocar 'ovr' ni 'manual' aquí tampoco.
void WebUI::handleModeManualStop() {
  Preferences p;
  if (p.begin(NS_MODE, /*ro*/ false)) {
    // Preserva resto: solo marca fin de ejecución
    p.putUChar("run", 0);
    p.end();
  }

  // El apagado real debe ejecutarse en tu lógica de control (leyendo 'run' = 0).
  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}
