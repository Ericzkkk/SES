#pragma once

#include <Arduino.h>

class MdsP2510 {
 public:
  enum class Status : uint8_t {
    Ok,
    InvalidArgument,
    Timeout,
    ShortFrame,
    WrongAddress,
    WrongFunction,
    WrongByteCount,
    CrcError,
    ModbusException,
  };

  // directionPin drives a typical transceiver's tied DE and /RE pins:
  // HIGH = transmit, LOW = receive. Use -1 for an auto-direction transceiver.
  MdsP2510(HardwareSerial& port, uint8_t deviceAddress = 1,
           int16_t directionPin = -1);

  void begin(uint32_t baud = 9600);
  void setTimeout(uint16_t timeoutMs);
  void setDeviceAddress(uint8_t deviceAddress);

  // The manual lists velocity at table registers 0003-0004. Its example proves
  // the table is one-based, so the Modbus PDU start address is 0x0002.
  Status readVelocity(float& metresPerSecond);
  Status readWaterLevel(float& metres);

  // Addresses passed here are zero-based Modbus PDU addresses.
  Status readHoldingRegisters(uint16_t startAddress, uint16_t registerCount,
                              uint16_t* output);

  uint8_t lastExceptionCode() const;
  static uint16_t modbusCrc16(const uint8_t* data, size_t length);

 private:
  static constexpr uint16_t kMaximumRegisters = 16;

  HardwareSerial& port_;
  uint8_t deviceAddress_;
  int16_t directionPin_;
  uint16_t timeoutMs_;
  uint8_t lastExceptionCode_;

  void setTransmitMode(bool enabled);
  void discardPendingInput();
  Status readFloat(uint16_t startAddress, float& value);
};
