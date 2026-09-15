#pragma once

#include "MdsP2510.h"
#include <math.h>

namespace BoSLFlow {

struct Sample {
  // False until begin() has been called and the startup interval has elapsed.
  bool ready = false;
  bool velocityValid = false;
  bool waterLevelValid = false;
  float velocityMps = NAN;
  float waterLevelM = NAN;
  uint32_t completedAtMs = 0;
  MdsP2510::Status velocityStatus = MdsP2510::Status::InvalidArgument;
  MdsP2510::Status waterLevelStatus = MdsP2510::Status::InvalidArgument;
  uint8_t velocityException = 0;
  uint8_t waterLevelException = 0;

  bool valid() const { return ready && velocityValid && waterLevelValid; }
};

// Call for each measurement cycle. Enables EN_SWVPP (MT3608/sensor),
// EN_SWB (RS485 converter), and UART2. Startup interval is configurable;
// the default is conservative, not a manufacturer-specified requirement.
void begin(uint32_t startupMs = 20000UL);
// PWR-06: stop UART, release TTL pins, then disable EN_SWB and EN_SWVPP.
void end();
bool ready();

// Call from the logger's sampling schedule, not an interrupt. Reads velocity
// and water level separately. Each request has a 500-ms response timeout.
// An error in one measurement does not invalidate the other measurement.
Sample read();

}  // namespace BoSLFlow
