#include "BoSLFlow.h"

#if !defined(EN_SWVPP) || !defined(EN_SWB) || !defined(TXD2) || !defined(RXD2)
#error Select BoSL Board 0.5.x with the AVR pinout.
#endif

namespace BoSLFlow {
namespace {
MdsP2510 sensor(Serial2, 1, -1);
bool started = false;
bool startupComplete = false;
uint32_t startedAtMs = 0;
uint32_t startupIntervalMs = 20000UL;
}

void begin(uint32_t startupMs) {
  // PWR-06: VPP_SW -> MT3608 -> sensor; SWB -> RS485 converter.
  digitalWrite(EN_SWVPP, HIGH);
  pinMode(EN_SWVPP, OUTPUT);
  // PWR-01: enable converter for this measurement cycle.
  digitalWrite(EN_SWB, HIGH);
  pinMode(EN_SWB, OUTPUT);
  digitalWrite(TXD2, HIGH);
  pinMode(TXD2, OUTPUT);
  pinMode(RXD2, INPUT);
  sensor.begin(9600);
  sensor.setTimeout(500);
  startupIntervalMs = startupMs < 200UL ? 200UL : startupMs;
  startedAtMs = millis();
  startupComplete = false;
  started = true;
}

void end() {
  if (started) Serial2.flush();
  Serial2.end();
  // Do not drive an idle HIGH into an unpowered converter.
  digitalWrite(TXD2, LOW);
  pinMode(TXD2, INPUT);
  digitalWrite(RXD2, LOW);
  pinMode(RXD2, INPUT);
  digitalWrite(EN_SWB, LOW);
  pinMode(EN_SWB, OUTPUT);
  // PWR-06: cut the boost converter input, not just its serial interface.
  digitalWrite(EN_SWVPP, LOW);
  pinMode(EN_SWVPP, OUTPUT);
  started = false;
  startupComplete = false;
}

bool ready() {
  if (!started) return false;
  if (!startupComplete &&
      static_cast<uint32_t>(millis() - startedAtMs) >= startupIntervalMs) {
    startupComplete = true;
  }
  return startupComplete;
}

Sample read() {
  Sample sample;
  if (!ready()) return sample;
  sample.ready = true;

  float value = NAN;
  sample.velocityStatus = sensor.readVelocity(value);
  sample.velocityException = sensor.lastExceptionCode();
  sample.velocityValid = sample.velocityStatus == MdsP2510::Status::Ok &&
                         isfinite(value);
  if (sample.velocityValid) sample.velocityMps = value;

  value = NAN;
  sample.waterLevelStatus = sensor.readWaterLevel(value);
  sample.waterLevelException = sensor.lastExceptionCode();
  sample.waterLevelValid = sample.waterLevelStatus == MdsP2510::Status::Ok &&
                           isfinite(value);
  if (sample.waterLevelValid) sample.waterLevelM = value;
  sample.completedAtMs = millis();
  return sample;
}

}  // namespace BoSLFlow
