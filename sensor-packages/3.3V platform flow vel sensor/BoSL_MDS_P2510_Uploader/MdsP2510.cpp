#include "MdsP2510.h"

#include <string.h>

namespace {
constexpr uint8_t kReadHoldingRegisters = 0x03;
constexpr uint8_t kExceptionFlag = 0x80;
constexpr uint8_t kRequestLength = 8;
constexpr uint8_t kResponseOverhead = 5;  // address, function, count, CRC16
constexpr uint16_t kInterFrameDelayMs = 5;  // >3.5 character times at 9600 8N1
}  // namespace

MdsP2510::MdsP2510(HardwareSerial& port, uint8_t deviceAddress,
                   int16_t directionPin)
    : port_(port),
      deviceAddress_(deviceAddress),
      directionPin_(directionPin),
      timeoutMs_(500),
      lastExceptionCode_(0) {}

void MdsP2510::begin(uint32_t baud) {
  if (directionPin_ >= 0) {
    pinMode(static_cast<uint8_t>(directionPin_), OUTPUT);
    setTransmitMode(false);
  }
  port_.begin(baud, SERIAL_8N1);
}

void MdsP2510::setTimeout(uint16_t timeoutMs) { timeoutMs_ = timeoutMs; }

void MdsP2510::setDeviceAddress(uint8_t deviceAddress) {
  deviceAddress_ = deviceAddress;
}

MdsP2510::Status MdsP2510::readVelocity(float& metresPerSecond) {
  return readFloat(0x0002, metresPerSecond);
}

MdsP2510::Status MdsP2510::readWaterLevel(float& metres) {
  return readFloat(0x0000, metres);
}

MdsP2510::Status MdsP2510::readFloat(uint16_t startAddress, float& value) {
  static_assert(sizeof(float) == 4, "MDS-P2510 requires a 32-bit IEEE-754 float");

  uint16_t registers[2] = {0, 0};
  const Status status = readHoldingRegisters(startAddress, 2, registers);
  if (status != Status::Ok) {
    return status;
  }

  // The manual specifies byte order 1,2,3,4 (ABCD / big-endian).
  const uint32_t raw = (static_cast<uint32_t>(registers[0]) << 16) |
                       static_cast<uint32_t>(registers[1]);
  memcpy(&value, &raw, sizeof(value));
  return Status::Ok;
}

MdsP2510::Status MdsP2510::readHoldingRegisters(uint16_t startAddress,
                                                uint16_t registerCount,
                                                uint16_t* output) {
  if (output == nullptr || registerCount == 0 ||
      registerCount > kMaximumRegisters || deviceAddress_ == 0 ||
      deviceAddress_ > 247) {
    return Status::InvalidArgument;
  }

  lastExceptionCode_ = 0;
  discardPendingInput();
  delay(kInterFrameDelayMs);

  uint8_t request[kRequestLength] = {
      deviceAddress_,
      kReadHoldingRegisters,
      static_cast<uint8_t>(startAddress >> 8),
      static_cast<uint8_t>(startAddress & 0xFF),
      static_cast<uint8_t>(registerCount >> 8),
      static_cast<uint8_t>(registerCount & 0xFF),
      0,
      0,
  };
  const uint16_t requestCrc = modbusCrc16(request, kRequestLength - 2);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);  // CRC low byte first
  request[7] = static_cast<uint8_t>(requestCrc >> 8);

  setTransmitMode(true);
  delayMicroseconds(20);
  port_.write(request, sizeof(request));
  port_.flush();  // Wait until the final stop bit has left the UART.
  delayMicroseconds(20);
  setTransmitMode(false);

  uint8_t response[kResponseOverhead + (2 * kMaximumRegisters)] = {0};
  size_t received = 0;
  size_t expectedLength = 0;
  const uint32_t startedAt = millis();

  while (static_cast<uint32_t>(millis() - startedAt) < timeoutMs_) {
    while (port_.available() > 0) {
      if (received >= sizeof(response)) {
        return Status::WrongByteCount;
      }
      response[received++] = static_cast<uint8_t>(port_.read());

      if (received == 2 && (response[1] & kExceptionFlag) != 0) {
        expectedLength = 5;
      } else if (received == 3 &&
                 (response[1] & kExceptionFlag) == 0) {
        expectedLength = static_cast<size_t>(response[2]) + kResponseOverhead;
        if (expectedLength > sizeof(response)) {
          return Status::WrongByteCount;
        }
      }

      if (expectedLength != 0 && received >= expectedLength) {
        break;
      }
    }

    if (expectedLength != 0 && received >= expectedLength) {
      break;
    }
  }

  if (received == 0) {
    return Status::Timeout;
  }
  if (expectedLength == 0 || received < expectedLength) {
    return Status::ShortFrame;
  }

  const uint16_t receivedCrc =
      static_cast<uint16_t>(response[expectedLength - 2]) |
      (static_cast<uint16_t>(response[expectedLength - 1]) << 8);
  const uint16_t calculatedCrc =
      modbusCrc16(response, expectedLength - 2);
  if (receivedCrc != calculatedCrc) {
    return Status::CrcError;
  }
  if (response[0] != deviceAddress_) {
    return Status::WrongAddress;
  }
  if (response[1] == (kReadHoldingRegisters | kExceptionFlag)) {
    lastExceptionCode_ = response[2];
    return Status::ModbusException;
  }
  if (response[1] != kReadHoldingRegisters) {
    return Status::WrongFunction;
  }

  const uint8_t expectedByteCount = static_cast<uint8_t>(registerCount * 2);
  if (response[2] != expectedByteCount) {
    return Status::WrongByteCount;
  }

  for (uint16_t index = 0; index < registerCount; ++index) {
    output[index] =
        (static_cast<uint16_t>(response[3 + (index * 2)]) << 8) |
        static_cast<uint16_t>(response[4 + (index * 2)]);
  }
  return Status::Ok;
}

uint8_t MdsP2510::lastExceptionCode() const { return lastExceptionCode_; }

uint16_t MdsP2510::modbusCrc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001) != 0) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void MdsP2510::setTransmitMode(bool enabled) {
  if (directionPin_ >= 0) {
    digitalWrite(static_cast<uint8_t>(directionPin_), enabled ? HIGH : LOW);
  }
}

void MdsP2510::discardPendingInput() {
  const uint32_t startedAt = millis();
  while (port_.available() > 0 &&
         static_cast<uint32_t>(millis() - startedAt) < timeoutMs_) {
    port_.read();
  }
}
