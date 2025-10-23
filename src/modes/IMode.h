#pragma once
#include <Arduino.h>

class IMode {
public:
  virtual ~IMode() {}
  virtual void begin() = 0;  // inicialización del modo
  virtual void run()   = 0;  // bucle
  virtual void reset() = 0;  // restablecer estado interno
};
