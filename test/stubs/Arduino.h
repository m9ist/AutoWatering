#pragma once
#include <algorithm>
#include <cstdint>
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
