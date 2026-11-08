// JsonWriter.h - minimal streaming JSON writer (no external libraries).
// Output goes to a std::string buffer; when the buffer exceeds flushAt bytes
// and a sink is installed, the buffer is handed to the sink (chunked HTTP).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>

class JsonWriter {
 public:
  typedef void (*Sink)(const std::string& chunk, void* ctx);

  explicit JsonWriter(size_t reserve = 512) { buf_.reserve(reserve); }

  void setSink(Sink s, void* ctx, size_t flushAt = 1400) {
    sink_ = s;
    ctx_ = ctx;
    flushAt_ = flushAt;
  }

  JsonWriter& beginObject() { sep(); buf_ += '{'; first_ = true; return *this; }
  JsonWriter& endObject() { buf_ += '}'; first_ = false; maybeFlush(); return *this; }
  JsonWriter& beginArray() { sep(); buf_ += '['; first_ = true; return *this; }
  JsonWriter& endArray() { buf_ += ']'; first_ = false; maybeFlush(); return *this; }

  JsonWriter& key(const char* k) {
    sep();
    quote(k);
    buf_ += ':';
    first_ = true;  // value follows without comma
    afterKey_ = true;
    return *this;
  }

  JsonWriter& value(const char* s) { sep(); if (s) quote(s); else buf_ += "null"; return *this; }
  JsonWriter& value(const std::string& s) { return value(s.c_str()); }
  JsonWriter& value(bool b) { sep(); buf_ += b ? "true" : "false"; return *this; }
  // int32_t is `long` on Xtensa and `int` on x86/RISC-V: cover both families.
  JsonWriter& value(long v) { char t[24]; snprintf(t, sizeof t, "%ld", v); return raw(t); }
  JsonWriter& value(unsigned long v) { char t[24]; snprintf(t, sizeof t, "%lu", v); return raw(t); }
  JsonWriter& value(int v) { return value((long)v); }
  JsonWriter& value(unsigned v) { return value((unsigned long)v); }
  JsonWriter& value(short v) { return value((long)v); }
  JsonWriter& value(unsigned short v) { return value((unsigned long)v); }
  JsonWriter& value(signed char v) { return value((long)v); }
  JsonWriter& value(unsigned char v) { return value((unsigned long)v); }
  JsonWriter& value(double v, int decimals = 3) {
    if (v != v) return null();  // NaN
    char t[32];
    snprintf(t, sizeof t, "%.*f", decimals, v);
    return raw(t);
  }
  JsonWriter& null() { return raw("null"); }
  JsonWriter& hex(uint8_t b) { char t[3]; snprintf(t, sizeof t, "%02X", b); return value(t); }
  JsonWriter& hexBytes(const uint8_t* p, size_t n, char sepChar = ' ') {
    std::string s;
    s.reserve(n * 3);
    char t[4];
    for (size_t i = 0; i < n; i++) {
      snprintf(t, sizeof t, "%02X", p[i]);
      if (i && sepChar) s += sepChar;
      s += t;
    }
    return value(s);
  }

  // key/value shortcuts
  template <typename T>
  JsonWriter& kv(const char* k, T v) { key(k); return value(v); }
  JsonWriter& kv(const char* k, double v, int decimals) { key(k); return value(v, decimals); }
  JsonWriter& kvNull(const char* k) { key(k); return null(); }

  const std::string& str() const { return buf_; }
  void clear() { buf_.clear(); first_ = true; afterKey_ = false; }
  void flush() {
    if (sink_ && !buf_.empty()) {
      sink_(buf_, ctx_);
      buf_.clear();
    }
  }

 private:
  JsonWriter& raw(const char* t) { sep(); buf_ += t; return *this; }
  void sep() {
    if (afterKey_) { afterKey_ = false; first_ = false; return; }
    if (!first_) buf_ += ',';
    first_ = false;
  }
  void quote(const char* s) {
    buf_ += '"';
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
      unsigned char c = *p;
      switch (c) {
        case '"': buf_ += "\\\""; break;
        case '\\': buf_ += "\\\\"; break;
        case '\n': buf_ += "\\n"; break;
        case '\r': buf_ += "\\r"; break;
        case '\t': buf_ += "\\t"; break;
        default:
          if (c < 0x20) {
            char t[8];
            snprintf(t, sizeof t, "\\u%04x", c);
            buf_ += t;
          } else {
            buf_ += (char)c;  // UTF-8 passes through
          }
      }
    }
    buf_ += '"';
  }
  void maybeFlush() {
    if (sink_ && buf_.size() >= flushAt_) flush();
  }

  std::string buf_;
  bool first_ = true;
  bool afterKey_ = false;
  Sink sink_ = nullptr;
  void* ctx_ = nullptr;
  size_t flushAt_ = 1400;
};
