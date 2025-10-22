#pragma once
#include <Arduino.h>

// Lector de líneas no bloqueante para un Stream (Serial).
// Entrega también líneas vacías (Enter) e ignora CRLF/LFCR dobles.
class LineReader {
public:
  explicit LineReader(Stream& io) : io_(io) {}

  // Devuelve true cuando completa una línea (incluyendo vacías).
  bool poll(String& out, size_t maxLen = 512) {
    while (io_.available()) {
      char c = (char)io_.read();
      if (c == '\r' || c == '\n') {
        // Swallow CRLF / LFCR
        if (io_.available()) {
          int p = io_.peek();
          if ((c == '\r' && p == '\n') || (c == '\n' && p == '\r')) (void)io_.read();
        }
        out = buf_;
        buf_ = "";
        return true;
      }
      if (isPrintable(c) && buf_.length() < maxLen) buf_ += c;
    }
    return false;
  }

private:
  Stream& io_;
  String  buf_;
};
