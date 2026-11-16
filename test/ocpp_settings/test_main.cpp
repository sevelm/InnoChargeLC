#include "ocpp_auth.hpp"
#include "ocpp_policy.hpp"
#include <assert.h>
#include <stdio.h>
#include <string>

static void test_existing_passwords() {
    uint8_t decoded[ocpp_auth::MAX_PASSWORD_BYTES]{};
    size_t length = 0;
    // Exhaust the existing printable ASCII alphabet at every accepted length.
    for (size_t size = 16; size <= 20; ++size) {
        for (int value = 0x21; value <= 0x7e; ++value) {
            const std::string password(size, char(value));
            assert(ocpp_auth::decode_password(password.c_str(), decoded, length));
            assert(length == size && !memcmp(decoded, password.data(), size));
        }
    }
    const char* legacyPrefix = "hex:123456789012";
    assert(ocpp_auth::decode_password(legacyPrefix, decoded, length));
    assert(length == strlen(legacyPrefix) && !memcmp(decoded, legacyPrefix, length));
    assert(!ocpp_auth::valid_password("123456789012345"));
    assert(!ocpp_auth::valid_password("123456789012345678901"));
    assert(!ocpp_auth::valid_password("123456789012345 "));
    assert(!ocpp_auth::valid_password(nullptr));
}

static void test_binary_passwords() {
    uint8_t decoded[ocpp_auth::MAX_PASSWORD_BYTES]{};
    size_t length = 0;
    assert(ocpp_auth::decode_password("hex:0001020304050607FFFFFFFFFFFFFFFFFFFFFFFF", decoded, length));
    assert(length == 20 && decoded[0] == 0 && decoded[7] == 7);
    for (size_t i = 8; i < length; ++i) assert(decoded[i] == 0xff);
    assert(ocpp_auth::decode_password("hex:000102030405060708090a0b0c0d0e0f", decoded, length));
    assert(length == 16);
    for (size_t i = 0; i < length; ++i) assert(decoded[i] == i);
    assert(!ocpp_auth::valid_password("hex:000102030405060708090a0b0c0d0e"));
    assert(!ocpp_auth::valid_password("hex:000102030405060708090a0b0c0d0e0"));
    assert(!ocpp_auth::valid_password("hex:000102030405060708090a0b0c0d0e0g"));
    assert(!ocpp_auth::valid_password("hex:000102030405060708090a0b0c0d0e0f0001020304"));
    // Invalid input must not partially overwrite the destination.
    memset(decoded, 0xa5, sizeof(decoded));
    assert(!ocpp_auth::decode_password("hex:000102030405060708090a0b0c0d0e0g", decoded, length));
    assert(length == 0);
    for (uint8_t value : decoded) assert(value == 0xa5);
}

static void test_retry_settings() {
    using namespace ocpp_policy;
    uint32_t value = 0;
    assert(retry_key("TransactionMessageAttempts") == RetryKey::Attempts);
    assert(retry_key("TransactionMessageRetryInterval") == RetryKey::Interval);
    assert(retry_key("ConnectionTimeOut") == RetryKey::Unsupported);
    assert(parse_retry(RetryKey::Attempts, "3", 1, value) && value == 3);
    assert(parse_retry(RetryKey::Attempts, "100", 3, value) && value == 100);
    assert(!parse_retry(RetryKey::Attempts, "0", 1, value));
    assert(!parse_retry(RetryKey::Attempts, "101", 3, value));
    assert(parse_retry(RetryKey::Interval, "60", 2, value) && value == 60);
    assert(parse_retry(RetryKey::Interval, "0", 1, value) && value == 0);
    assert(parse_retry(RetryKey::Interval, "86400", 5, value) && value == 86400);
    assert(!parse_retry(RetryKey::Interval, "86401", 5, value));
    assert(!parse_retry(RetryKey::Interval, "-1", 2, value));
    assert(!parse_retry(RetryKey::Interval, "1.0", 3, value));
    assert(!parse_retry(RetryKey::Interval, " 60", 3, value));
    assert(!parse_retry(RetryKey::Interval, "60\0extra", 8, value));
    assert(!parse_retry(RetryKey::Interval, "999999999999", 12, value));
    assert(!parse_retry(RetryKey::Unsupported, "60", 2, value));
    // Existing settings retain their current validation rules.
    assert(parse_interval(Interval::Heartbeat, "60", 2, value) && value == 60);
    assert(!parse_interval(Interval::Heartbeat, "0", 1, value));
    assert(parse_interval(Interval::MeterValues, "0", 1, value) && value == 0);
}

int main() {
    test_existing_passwords();
    test_binary_passwords();
    test_retry_settings();
    puts("PASS: legacy password bytes, binary keys, retry settings and existing intervals");
}
