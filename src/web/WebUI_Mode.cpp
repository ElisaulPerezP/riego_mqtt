// File: src/web/WebUI_Mode.cpp
#include "web/WebUI.h"
#include <Preferences.h>
#include "modes/modes.h"         // manualWeb_* , resetFullMode, ManualTelemetry
#include "../state/RelayState.h"

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
      if (states[i].name.length()) s += states[i].name;
      else { s += F("Estado "); s += String((int)i); }
      s += F("</option>");
    }
    s += F("</select></label></p>");

    // Porcentajes fertilizantes (guardados, no aplicados aquí aún)
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

    // ====== Telemetría en vivo (cada 10s) ======
    ManualTelemetry mt = modesGetManualTelemetry();
    s += F("<div class='formcard'>"
           "<h5>Volumen entregado (vivo)</h5>");
    s += F("<p><b>");
    s += String((unsigned long)mt.volumeMl);
    s += F("</b> mL &nbsp; <small>(");
    s += String((unsigned long)(mt.elapsedMs/1000));
    s += F(" s)</small></p>");
    s += F("<div style='font-size:12px;color:#666'>Se reinicia automáticamente al cambiar de zona.</div>"
           "</div>");

    // Si está corriendo, botón Parar (sin tocar ovr/manual)
    if (running) {
      s += F("<form method='post' action='/mode/manual/stop'>");
      s += F("<p><button class='btn'>Parar</button></p>");
      s += F("</form>");
    }

    // Auto-refresh cada 10s mientras override+manual esté activo
    s += F("<script>setTimeout(function(){ location.reload(); }, 10000);</script>");

    s += F("</div>");
  }

  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

// =============== POST: /mode/set =================
void WebUI::handleModeSet() {
  bool ovr    = server_.hasArg("ovr");
  bool manual = (server_.hasArg("mode") && server_.arg("mode") == "manual");

  Preferences p;
  if (p.begin(NS_MODE, /*ro*/ false)) {
    p.putUChar("ovr",    ovr ? 1 : 0);
    p.putUChar("manual", manual ? 1 : 0);
    p.end();
  }

  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}

// =============== POST: /mode/manual/start =================
void WebUI::handleModeManualStart() {
  int sel = server_.hasArg("sel") ? server_.arg("sel").toInt() : -1;
  uint8_t p1 = server_.hasArg("p1") ? (uint8_t)constrain(server_.arg("p1").toInt(), 0, 100) : 0;
  uint8_t p2 = server_.hasArg("p2") ? (uint8_t)constrain(server_.arg("p2").toInt(), 0, 100) : 0;

  if (!getStates_) { server_.send(500, F("text/plain"), F("States API no inicializada")); return; }
  std::vector<RelayState> states = getStates_();
  if (sel < 0 || sel >= (int)states.size()) { server_.send(400, F("text/plain"), F("Índice de estado inválido")); return; }
  const RelayState& rs = states[sel];

  {
    Preferences p;
    if (p.begin(NS_MODE, /*ro*/ false)) {
      p.putUChar("ovr",    1);
      p.putUChar("manual", 1);
      p.putInt   ("sel", sel);
      p.putUChar ("p1",  p1);
      p.putUChar ("p2",  p2);
      p.putUChar ("run", 1);
      p.putULong ("run_since", millis());
      p.end();
    }
  }

  resetFullMode();
  manualWeb_startState(rs);

  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}

// =============== POST: /mode/manual/stop =================
void WebUI::handleModeManualStop() {
  {
    Preferences p;
    if (p.begin(NS_MODE, /*ro*/ false)) {
      p.putUChar("run", 0);
      p.end();
    }
  }

  manualWeb_stopState();

  server_.sendHeader(F("Location"), "/mode");
  server_.send(302, F("text/plain"), "");
}
