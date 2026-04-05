#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string.h>
#include <cmsis_compiler.h>

// using std::min;
// using std::max;

/**
 * Dummy definition for arduino::UART
 */
namespace arduino {
    class UART {
    public:
        size_t availableForWrite();
        int read();
        uint16_t write(uint8_t byte);
    };
}

/**
 * Non-blocking buffered serial for Arduino UART.
 * Thread-safe on 32-bit platforms.
 */
class BufferedSerial {
public:
    BufferedSerial(arduino::UART* serial);

    uint16_t available();

    uint16_t write(const char* buf, uint16_t len);
    uint16_t read(char* buf, uint16_t len);

    uint16_t receive();
    uint16_t transmit();

    
    void step();
};

#endif // UTILS_SERIAL_H