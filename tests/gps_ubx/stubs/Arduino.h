#pragma once
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using byte = uint8_t;
using String = std::string;
inline uint32_t test_millis = 1000;
inline uint32_t millis() { return test_millis++; }
inline void delay(uint32_t ms) { test_millis += ms; }
constexpr double TWO_PI = 6.28318530717958647692;
inline double radians(double value) { return value * TWO_PI / 360.0; }
inline double degrees(double value) { return value * 360.0 / TWO_PI; }
inline double sq(double value) { return value * value; }

class Stream
{
  public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t* data, size_t length)
    {
        for (size_t i = 0; i < length; ++i) write(data[i]);
        return length;
    }
    size_t readBytes(uint8_t* data, size_t length)
    {
        size_t count = 0;
        const uint32_t start = millis();
        while (count < length && millis() - start < 1000)
            if (available()) data[count++] = static_cast<uint8_t>(read());
        return count;
    }
};

struct FakeSerial
{
    template <typename... Args>
    int printf(const char*, Args...) { return 0; }
    int available() const { return 0; }
    int read() const { return -1; }
    size_t write(int) { return 1; }
};
inline FakeSerial Serial;
#define log_i(...) ((void)0)
#define log_e(...) ((void)0)
#define log_d(...) ((void)0)

// Host checks must exit normally instead of opening Windows crash-report UI.
#undef assert
#define assert(condition)                                                                     \
    do                                                                                        \
    {                                                                                         \
        if (!(condition))                                                                     \
        {                                                                                     \
            std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1);                                                                     \
        }                                                                                     \
    } while (0)
