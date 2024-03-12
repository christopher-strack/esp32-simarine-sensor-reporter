#include <cstdio>
#include <span>
#include <string_view>

class JsonWriter {
public:
  explicit JsonWriter(const std::span<char> buffer)
      : mBuffer{buffer}, mPos{0} {}

  void writeChar(char c) {
    if (mPos < mBuffer.size() - 1) {
      mBuffer[mPos] = c;
      mPos++;
    }
  }

  template <typename... Args>
  void writeString(const char *str, Args &&...args) {
    const auto length =
        std::snprintf(mBuffer.data() + mPos, mBuffer.size() - mPos, str,
                      std::forward<Args>(args)...);
    mPos += length;
  }

  std::string_view toString() const { return {mBuffer.data(), mBuffer.size()}; }

private:
  const std::span<char> mBuffer;
  size_t mPos;
};
