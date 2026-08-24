/**
 * @file WString.h
 * @brief Minimal Arduino String stand-in for host-run tests.
 *
 * The native env has no Arduino core. This provides just enough of the
 * String API for signalk_metadata.cpp, plus an ArduinoJson custom
 * converter so `json["key"] = String(...)` serializes as a JSON string.
 */

#ifndef SENSESP_TEST_NATIVE_TEST_SK_METADATA_WSTRING_H_
#define SENSESP_TEST_NATIVE_TEST_SK_METADATA_WSTRING_H_

#include <ArduinoJson.h>
#include <string>

class String : public std::string {
 public:
  String() = default;
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
};

inline void convertToJson(const String& src, ArduinoJson::JsonVariant dst) {
  dst.set(src.c_str());
}

#endif
