/* Wojtek — MM32 Gen2 UART protocol interface */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm32 {

constexpr uint8_t kCommandStart = '/';
constexpr uint16_t kTelemetryStart = 0xABCD;
constexpr uint32_t kDefaultBaud = 19200;
constexpr size_t kCommandV0Size = 8;
constexpr size_t kCommandMasterSize = 10;
constexpr size_t kTelemetrySize = 15;

// The MM32 Gen2 UART bus uses CRC-16/CCITT (poly 0x1021, init 0).
uint16_t crc16_ccitt(const uint8_t* data, size_t length);

// Encodes addressed command type 0. Wire format:
// '/', type=0, slave id, speed LE, state, CRC LE.
bool encode_speed_command(uint8_t slave_id, int16_t speed, uint8_t state,
                          uint8_t* out, size_t capacity);

// Decoded telemetry values; units are those used by the MM32 firmware:
// speed = 0.1 km/h, voltage/current = firmware-scaled raw values,
// odometer = hall steps.
struct Telemetry {
  uint8_t slave_id = 0;
  int16_t speed = 0;
  uint16_t voltage = 0;
  int16_t current = 0;
  int32_t odometer = 0;
};

enum class ParseResult : uint8_t { None, Frame, BadCrc };

class TelemetryParser {
 public:
  ParseResult push(uint8_t byte, Telemetry& out);
  void reset();

 private:
  uint8_t buffer_[kTelemetrySize]{};
  size_t used_ = 0;
};

}  // namespace mm32
