#include <unity.h>
#include "mm32_protocol.h"

void test_crc_ccitt_known_vector() {
  const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
  TEST_ASSERT_EQUAL_HEX16(0x31C3, mm32::crc16_ccitt(data, sizeof(data)));
}
void test_speed_command_layout_and_crc() {
  uint8_t frame[mm32::kCommandV0Size]{};
  TEST_ASSERT_TRUE(mm32::encode_speed_command(7, -321, 0x40, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8('/', frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(7, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(0xBF, frame[3]);
  TEST_ASSERT_EQUAL_UINT8(0xFE, frame[4]);
  TEST_ASSERT_EQUAL_UINT8(0x40, frame[5]);
  const uint16_t crc = mm32::crc16_ccitt(frame, 6);
  TEST_ASSERT_EQUAL_UINT8(crc & 0xff, frame[6]);
  TEST_ASSERT_EQUAL_UINT8(crc >> 8, frame[7]);
}
void test_command_rejects_small_buffer() {
  uint8_t frame[7]{};
  TEST_ASSERT_FALSE(mm32::encode_speed_command(1, 0, 0, frame, sizeof(frame)));
}
void test_telemetry_parser_decodes_frame() {
  uint8_t frame[mm32::kTelemetrySize] = {
    0xCD, 0xAB, 0x03, 0x34, 0x12, 0x78, 0x56,
    0xFE, 0xFF, 0x04, 0x03, 0x02, 0x01, 0, 0
  };
  const uint16_t crc = mm32::crc16_ccitt(frame, mm32::kTelemetrySize - 2);
  frame[13] = crc & 0xff;
  frame[14] = crc >> 8;
  mm32::TelemetryParser parser;
  mm32::Telemetry telemetry;
  mm32::ParseResult result = mm32::ParseResult::None;
  for (uint8_t byte : frame) result = parser.push(byte, telemetry);
  TEST_ASSERT_EQUAL(static_cast<int>(mm32::ParseResult::Frame), static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT8(3, telemetry.slave_id);
  TEST_ASSERT_EQUAL_INT16(0x1234, telemetry.speed);
  TEST_ASSERT_EQUAL_UINT16(0x5678, telemetry.voltage);
  TEST_ASSERT_EQUAL_INT16(-2, telemetry.current);
  TEST_ASSERT_EQUAL_INT32(0x01020304, telemetry.odometer);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_ccitt_known_vector);
  RUN_TEST(test_speed_command_layout_and_crc);
  RUN_TEST(test_command_rejects_small_buffer);
  RUN_TEST(test_telemetry_parser_decodes_frame);
  return UNITY_END();
}
