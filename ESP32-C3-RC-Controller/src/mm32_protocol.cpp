// Wojtek — MM32 Gen2 UART protocol implementation
#include "mm32_protocol.h"

namespace mm32 {
namespace {
uint16_t read_u16_le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}
int16_t read_i16_le(const uint8_t* p) {
  return static_cast<int16_t>(read_u16_le(p));
}
uint32_t read_u32_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void write_u16_le(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xff);
  p[1] = static_cast<uint8_t>(v >> 8);
}
}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0;
  for (size_t n = 0; n < length; ++n) {
    crc ^= static_cast<uint16_t>(data[n]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool encode_speed_command(uint8_t slave_id, int16_t speed, uint8_t state,
                          uint8_t* out, size_t capacity) {
  if (out == nullptr || capacity < kCommandV0Size) return false;
  out[0] = kCommandStart;
  out[1] = 0;
  out[2] = slave_id;
  write_u16_le(out + 3, static_cast<uint16_t>(speed));
  out[5] = state;
  write_u16_le(out + 6, crc16_ccitt(out, 6));
  return true;
}

void TelemetryParser::reset() { used_ = 0; }

ParseResult TelemetryParser::push(uint8_t byte, Telemetry& out) {
  // Seek the little-endian bytes CD AB for START_FRAME 0xABCD.
  if (used_ == 0) {
    if (byte == 0xCD) buffer_[used_++] = byte;
    return ParseResult::None;
  }
  if (used_ == 1 && byte != 0xAB) {
    used_ = (byte == 0xCD) ? 1 : 0;
    if (used_) buffer_[0] = byte;
    return ParseResult::None;
  }
  buffer_[used_++] = byte;
  if (used_ < kTelemetrySize) return ParseResult::None;

  const uint16_t received = read_u16_le(buffer_ + kTelemetrySize - 2);
  const uint16_t expected = crc16_ccitt(buffer_, kTelemetrySize - 2);
  if (received != expected) {
    // Retain a possible first sync byte at the end for resynchronization.
    const bool trailing_cd = buffer_[kTelemetrySize - 1] == 0xCD;
    used_ = trailing_cd ? 1 : 0;
    if (trailing_cd) buffer_[0] = 0xCD;
    return ParseResult::BadCrc;
  }

  out.slave_id = buffer_[2];
  out.speed = read_i16_le(buffer_ + 3);
  out.voltage = read_u16_le(buffer_ + 5);
  out.current = read_i16_le(buffer_ + 7);
  out.odometer = static_cast<int32_t>(read_u32_le(buffer_ + 9));
  used_ = 0;
  return ParseResult::Frame;
}
}  // namespace mm32
