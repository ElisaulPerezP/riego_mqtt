#pragma once
#include <Arduino.h>

class LineReader {
public:
  explicit LineReader(HardwareSerial* s = &Serial) : io_(s) {}

  // Lee una línea de forma no bloqueante. Devuelve true si entregó una.
  bool poll(String& out, size_t maxLen = 512);

  void setStream(HardwareSerial* s) { io_ = s; }

private:
  HardwareSerial* io_ = &Serial;
  String buf_;
};
