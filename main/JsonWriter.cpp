#include "JsonWriter.hpp"

void JsonWriter::startArray() {
  handleComma();
  writeChar('[');
  mCommaRequired = false;
}

void JsonWriter::endArray() {
  writeChar(']');
  mCommaRequired = true;
}

void JsonWriter::startObject() {
  handleComma();
  writeChar('{');
  mCommaRequired = false;
}

void JsonWriter::endObject() {
  writeChar('}');
  mCommaRequired = true;
  mExpectObjectValue = false;
}

void JsonWriter::addObjectKey(std::string_view key) {
  handleComma();
  writeChar('"');
  writeData(key);
  writeChar('"');
  writeChar(':');
  mCommaRequired = true;
  mExpectObjectValue = true;
}

void JsonWriter::addInt(int value) {
  handleCommaForValue();
  writeFormattedValue("%d", value);
  mCommaRequired = true;
}

void JsonWriter::addDouble(double value) {
  handleCommaForValue();
  writeFormattedValue("%g", value);
  mCommaRequired = true;
}

std::string_view JsonWriter::string() const { return {mBuffer.data(), mPos}; }
void JsonWriter::writeData(std::span<const char> data) {
  reserve(data.size());
  std::copy(data.begin(), data.end(), mBuffer.begin() + mPos);
  mPos += data.size();
}

void JsonWriter::writeChar(char c) { writeData(std::array<const char, 1>{c}); }

template <typename T>
void JsonWriter::writeFormattedValue(const char* formatString, T value) {
  auto write = [&]() {
    std::span<char> writeBuffer{mBuffer.data() + mPos,
                                mBuffer.size() - mPos + 1};
    return std::snprintf(writeBuffer.data(), writeBuffer.size(), formatString,
                         value);
  };

  const auto length = write();

  if (reserve(length)) {
    // Formatted value didn't fit the first time so let's write it again
    write();
  }

  mPos += length;
}

template void JsonWriter::writeFormattedValue<int>(const char* formatString,
                                                   int value);
template void JsonWriter::writeFormattedValue<double>(const char* formatString,
                                                      double value);

bool JsonWriter::reserve(size_t size) {
  if (mBuffer.size() - mPos < size) {
    mBuffer.resize(mPos + size);
    return true;
  }
  return false;
}

void JsonWriter::handleCommaForValue() {
  if (!mExpectObjectValue) {
    handleComma();
    mExpectObjectValue = false;
  }
}

void JsonWriter::handleComma() {
  if (mCommaRequired) {
    writeChar(',');
  }
}
