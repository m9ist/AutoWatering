#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#define F(x) (x)

using std::max;
using std::min;

class String {
 public:
  std::string _s;

  String() {}
  String(const char* c) : _s(c ? c : "") {}
  String(int n) : _s(std::to_string(n)) {}
  String(char c) : _s(1, c) {}

  String& operator+=(const char* c) {
    if (c) _s += c;
    return *this;
  }
  String& operator+=(char c) {
    _s += c;
    return *this;
  }
  String& operator+=(int n) {
    _s += std::to_string(n);
    return *this;
  }
  String& operator+=(unsigned int n) {
    _s += std::to_string(n);
    return *this;
  }
  String& operator+=(const String& o) {
    _s += o._s;
    return *this;
  }

  size_t length() const { return _s.length(); }
  bool isEmpty() const { return _s.empty(); }
  const char* c_str() const { return _s.c_str(); }

  bool startsWith(const String& prefix) const {
    return _s.rfind(prefix._s, 0) == 0;
  }
  bool endsWith(const String& suffix) const {
    if (suffix._s.length() > _s.length()) return false;
    return _s.compare(_s.length() - suffix._s.length(), suffix._s.length(),
                      suffix._s) == 0;
  }
  int indexOf(char c, unsigned int from = 0) const {
    size_t pos = _s.find(c, from);
    return pos == std::string::npos ? -1 : (int)pos;
  }
  String substring(unsigned int from, unsigned int to) const {
    if (from >= _s.length()) return String();
    if (to > _s.length()) to = _s.length();
    if (to <= from) return String();
    return String(_s.substr(from, to - from).c_str());
  }
  char charAt(unsigned int i) const { return i < _s.length() ? _s[i] : 0; }
  long toInt() const { return strtol(_s.c_str(), nullptr, 10); }
  bool operator==(const String& o) const { return _s == o._s; }
};

inline String operator+(const String& a, const String& b) {
  String r = a;
  r += b;
  return r;
}
inline String operator+(const char* a, const String& b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String& a, const char* b) {
  String r = a;
  r += b;
  return r;
}

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

class Print {
 public:
  virtual size_t print(const char*) { return 0; }
  virtual size_t print(int) { return 0; }
  virtual size_t print(unsigned int) { return 0; }
  virtual size_t print(float) { return 0; }
  virtual size_t println(const char*) { return 0; }
  virtual size_t println(int) { return 0; }
  virtual size_t println() { return 0; }
  virtual ~Print() = default;
};
