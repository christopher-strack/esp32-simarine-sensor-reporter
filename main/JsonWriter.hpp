#pragma once

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

/*! A simple JSON string writer that reuses a buffer to reduce
 *  allocations.
 *
 *  Strings are not escaped.
 *  Inputs are not validated.
 *  Order of methods are not validated.
 */
using JsonBuffer = std::vector<char>;

class JsonWriter {
public:
  explicit JsonWriter(JsonBuffer& buffer) : mBuffer{buffer} {}

  void startArray();
  void endArray();

  void startObject();
  void endObject();

  void addObjectKey(std::string_view key);
  void addInt(int value);
  void addDouble(double value);

  std::string_view string() const;

private:
  void writeData(std::span<const char> data);
  void writeChar(char c);

  template <typename T>
  void writeFormattedValue(const char* formatString, T value);

  bool reserve(size_t size);

  void handleCommaForValue();
  void handleComma();

  JsonBuffer& mBuffer;
  size_t mPos{0};
  bool mCommaRequired{false};
  bool mExpectObjectValue{false};
};
