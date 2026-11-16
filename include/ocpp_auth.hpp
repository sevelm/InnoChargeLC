#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ocpp_auth {
constexpr size_t MIN_PASSWORD_BYTES = 16;
constexpr size_t MAX_PASSWORD_BYTES = 20;

inline int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

// Existing plaintext passwords keep their exact bytes. Longer "hex:" values
// store binary keys in the existing NUL-terminated NVS field, without a migration.
// A null output validates only; callers provide MAX_PASSWORD_BYTES otherwise.
inline bool decode_password(const char* stored, uint8_t* output, size_t& length) {
    length = 0;
    if (!stored) return false;
    const size_t size = strlen(stored);
    const bool hex = size > MAX_PASSWORD_BYTES && !strncmp(stored, "hex:", 4);
    if (hex) {
        const size_t digits = size - 4;
        if (digits < MIN_PASSWORD_BYTES * 2 || digits > MAX_PASSWORD_BYTES * 2 || digits % 2)
            return false;
        for (size_t i = 0; i < digits; ++i)
            if (hex_digit(stored[4 + i]) < 0) return false;
        if (output) {
            for (size_t i = 0; i < digits / 2; ++i)
                output[i] = uint8_t(hex_digit(stored[4 + i * 2]) * 16 + hex_digit(stored[5 + i * 2]));
        }
        length = digits / 2;
        return true;
    }
    if (size < MIN_PASSWORD_BYTES || size > MAX_PASSWORD_BYTES) return false;
    for (size_t i = 0; i < size; ++i)
        if (uint8_t(stored[i]) < 0x21 || uint8_t(stored[i]) > 0x7e) return false;
    if (output) memcpy(output, stored, size);
    length = size;
    return true;
}

inline bool valid_password(const char* stored) {
    size_t length = 0;
    return decode_password(stored, nullptr, length);
}
} // namespace ocpp_auth
