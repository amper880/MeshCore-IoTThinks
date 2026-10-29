// helpers/sensors/EpeverProtocol.h: Modbus RTU framing and Epever register
// decoding.
#include <gtest/gtest.h>
#include <helpers/sensors/EpeverProtocol.h>

#include <cmath>
#include <vector>

namespace {
std::vector<uint8_t> with_crc(std::vector<uint8_t> body) {
  uint16_t crc = modbus_crc16(body.data(), body.size());
  body.push_back((uint8_t)crc);
  body.push_back((uint8_t)(crc >> 8));
  return body;
}
}  // namespace

TEST(EpeverDecode, ScalingAndPvPowerPair) {
  // 18.52 V, 3.10 A, 70000 (L 0x1170, H 0x0001) = 700.00 W, 13.28 V, 5.25 A
  const uint16_t block[EPEVER_PV_BLOCK_LEN] = {1852, 310, 0x1170, 0x0001, 1328, 525, 6972, 0};
  EpeverReading r = {};
  epever_decode_pv_block(block, &r);
  epever_decode_temperature(2150, &r);
  EXPECT_NEAR(r.pv_voltage_v, 18.52f, 0.001f);
  EXPECT_NEAR(r.pv_current_a, 3.10f, 0.001f);
  EXPECT_NEAR(r.pv_power_w, 700.0f, 0.001f);
  EXPECT_NEAR(r.batt_voltage_v, 13.28f, 0.001f);
  EXPECT_NEAR(r.batt_charge_current_a, 5.25f, 0.001f);
  EXPECT_NEAR(r.batt_temperature_c, 21.5f, 0.001f);
  EXPECT_TRUE(r.has_temperature);
  EXPECT_FALSE(r.has_load);
}

TEST(EpeverDecode, TemperatureIsSigned) {
  EpeverReading r = {};
  epever_decode_temperature((uint16_t)-725, &r);
  EXPECT_NEAR(r.batt_temperature_c, -7.25f, 0.001f);
}

TEST(EpeverDecode, LoadRegisters) {
  const uint16_t load[EPEVER_LOAD_LEN] = {1328, 47};
  EpeverReading r = {};
  epever_decode_load(load, &r);
  EXPECT_NEAR(r.load_voltage_v, 13.28f, 0.001f);
  EXPECT_NEAR(r.load_current_a, 0.47f, 0.001f);
  EXPECT_TRUE(r.has_load);
}

TEST(Modbus, RequestCrcMatchesSpec) {
  uint8_t req[8];
  modbus_build_read_input(req, 1, 0x3100, 8);
  EXPECT_EQ(modbus_crc16(req, 8), 0);   // a frame with its CRC appended checks to 0
  // Modbus spec example: read 10 holding registers from unit 1 -> CRC 0xCDC5
  const uint8_t ex[6] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
  EXPECT_EQ(modbus_crc16(ex, 6), 0xCDC5);
}

TEST(Modbus, AnswerBehindEchoAndStrayByte) {
  uint8_t req[8];
  modbus_build_read_input(req, 1, 0x3110, 1);
  std::vector<uint8_t> rx(req, req + 8);
  rx.push_back(0x00);
  auto a = with_crc({0x01, 0x04, 0x02, 0x08, 0x66});
  rx.insert(rx.end(), a.begin(), a.end());

  uint16_t reg = 0;
  EXPECT_EQ(modbus_find_response(rx.data(), rx.size(), 1, 1, &reg), 0);
  EXPECT_EQ(reg, 0x0866);
  EXPECT_TRUE(modbus_response_complete(rx.data(), rx.size(), 1, 1));
  // truncated: not complete yet
  EXPECT_FALSE(modbus_response_complete(rx.data(), rx.size() - 1, 1, 1));
  EXPECT_EQ(modbus_find_response(rx.data(), rx.size() - 1, 1, 1, &reg), MODBUS_ERR_BADMSG);
  // the echo alone is never an answer
  EXPECT_EQ(modbus_find_response(req, 8, 1, 1, &reg), MODBUS_ERR_BADMSG);
}

TEST(Modbus, PvBlockAnswer) {
  std::vector<uint8_t> body = {0x01, 0x04, 16};
  const uint16_t regs[8] = {1852, 310, 0x1170, 0x0001, 1328, 525, 6972, 0};
  for (uint16_t v : regs) { body.push_back(v >> 8); body.push_back(v & 0xFF); }
  auto a = with_crc(body);
  uint16_t out[8] = {};
  EXPECT_EQ(modbus_find_response(a.data(), a.size(), 1, 8, out), 0);
  for (int i = 0; i < 8; i++) EXPECT_EQ(out[i], regs[i]);
  // wrong unit id
  EXPECT_EQ(modbus_find_response(a.data(), a.size(), 2, 8, out), MODBUS_ERR_BADMSG);
}

TEST(Modbus, ExceptionAndBadCrc) {
  auto ex = with_crc({0x01, 0x84, 0x02});
  uint16_t reg = 0;
  EXPECT_EQ(modbus_find_response(ex.data(), ex.size(), 1, 1, &reg), 2);
  auto bad = with_crc({0x01, 0x04, 0x02, 0x08, 0x66});
  bad.back() ^= 0xFF;
  EXPECT_EQ(modbus_find_response(bad.data(), bad.size(), 1, 1, &reg), MODBUS_ERR_BADMSG);
  EXPECT_EQ(modbus_find_response(nullptr, 0, 1, 1, &reg), MODBUS_ERR_TIMEOUT);
}
