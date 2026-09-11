/* BoSL 0.5.x live M2M One / Telstra NB-IoT test.
   Production power sequence retained. Send ! at 9600 baud to start once.
   APN: telstra.iot. NB-IoT only, B28. No application payload transmitted.
   Leaves modem on for inspection; original settings are NOT restored.
*/
#include <Arduino.h>
#include <avr/power.h>

#define MODEM Serial1

static const uint32_t USB_BAUD = 9600;
static const uint32_t MODEM_BAUD = 9600;
static const uint32_t SHORT_TIMEOUT_MS = 5000UL;
static const uint32_t NETWORK_TIMEOUT_MS = 180000UL;

// CUSTOMER SETTINGS ---------------------------------------------------------
// APN is not needed to hear a cell, but is configured before LTE/EPS attach.
#define CUSTOMER_APN "telstra.iot"

// Test only bands relevant to the deployment to avoid an hours-long scan.
// Australia normally uses Band 28 for Telstra LTE-M and NB-IoT.
// For another country, replace these lists with the operator's IoT bands.
static const uint8_t CAT_M_TEST_BANDS[] = {28};
static const uint8_t NB_IOT_TEST_BANDS[] = {28};

// Six minutes per band allowed; successful or rejected attempts stop early.
static const uint8_t REGISTRATION_SAMPLES = 24;
static const uint32_t REGISTRATION_SAMPLE_INTERVAL_MS = 15000UL;
// --------------------------------------------------------------------------

// Used by productionDelay(), copied from the production firmware's xDelay().
extern volatile unsigned long timer0_millis;

enum AtResult : uint8_t {
  AT_OK,
  AT_ERROR,
  AT_TIMEOUT
};

static bool lastCeregRegistered = false;
static int8_t lastCeregState = -1;

static void heading(const __FlashStringHelper *text) {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.println(text);
  Serial.println(F("============================================================"));
}

static void flushOldModemData() {
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 150UL) {
    while (MODEM.available()) {
      Serial.write(MODEM.read());
    }
  }
}

/*
  Streams the response directly to USB instead of storing the complete response.
  This is important because AT+COPS=? can return much more than 254 bytes.
*/
static AtResult sendAT(const char *command, uint32_t timeoutMs) {
  flushOldModemData();

  const bool isCeregQuery = strcmp(command, "AT+CEREG?") == 0;
  if (isCeregQuery) {
    lastCeregRegistered = false;
    lastCeregState = -1;
  }

  Serial.println();
  Serial.print(F(">>> "));
  Serial.println(command);

  MODEM.print(command);
  MODEM.print('\r');

  char line[80];
  size_t length = 0;
  bool oversizedLine = false;
  AtResult result = AT_TIMEOUT;

  const uint32_t started = millis();
  uint32_t lastProgress = started;
  bool finished = false;

  while (!finished && (uint32_t)(millis() - started) < timeoutMs) {
    while (MODEM.available()) {
      const char incoming = (char)MODEM.read();
      Serial.write(incoming);

      if (incoming == '\r') {
        continue;
      }

      if (incoming == '\n') {
        if (!oversizedLine) {
          line[length] = '\0';

          if (isCeregQuery && strncmp(line, "+CEREG:", 7) == 0) {
            int reportMode = -1;
            int registrationState = -1;
            if (sscanf(line, "+CEREG: %d,%d", &reportMode,
                       &registrationState) == 2) {
              lastCeregState = registrationState;
              lastCeregRegistered = registrationState == 1 ||
                                    registrationState == 5;
            }
          }

          if (strcmp(line, "OK") == 0) {
            result = AT_OK;
            finished = true;
          } else if (strcmp(line, "ERROR") == 0 ||
                     strstr(line, "+CME ERROR:") != NULL ||
                     strstr(line, "+CMS ERROR:") != NULL) {
            result = AT_ERROR;
            finished = true;
          }
        }

        length = 0;
        oversizedLine = false;
      } else if (length < sizeof(line) - 1) {
        line[length++] = incoming;
      } else {
        // Continue streaming the long line, but do not store it in RAM.
        oversizedLine = true;
      }
    }

    if (timeoutMs >= 60000UL &&
        (uint32_t)(millis() - lastProgress) >= 30000UL) {
      Serial.print(F("\r\n[waiting: "));
      Serial.print((uint32_t)(millis() - started) / 1000UL);
      Serial.println(F(" seconds elapsed]"));
      lastProgress = millis();
    }

    delay(1);
  }

  Serial.println();
  Serial.print(F("<<< "));
  if (result == AT_OK) {
    Serial.println(F("OK"));
  } else if (result == AT_ERROR) {
    Serial.println(F("MODEM ERROR"));
  } else {
    Serial.println(F("TIMEOUT"));
  }

  return result;
}

/*
  This is the production firmware's xDelay() implementation. It is retained so
  that PWRKEY, reset and startup timing follow the deployed firmware exactly.
*/
static void productionDelay(uint32_t durationMs) {
  if (Serial) {
    Serial.flush();
  }

  const uint32_t slowedDelayMs = durationMs / 64UL;
  clock_prescale_set(clock_div_64);
  delay(slowedDelayMs);
  clock_prescale_set(clock_div_1);

  cli();
  timer0_millis += 63UL * slowedDelayMs;
  sei();

  delay(durationMs - 64UL * slowedDelayMs);
}

static bool isSimComOn() {
  return digitalRead(SIM_STATUS_BFD);
}

static void initialiseProductionPins() {
  // Exact SIM7000 setup pin sequence from the production sketch.
  pinMode(SIM_BUF_EN, OUTPUT);
  digitalWrite(SIM_BUF_EN, HIGH);

  pinMode(SIM_PWRKEY, OUTPUT);
  digitalWrite(SIM_PWRKEY, HIGH);

  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, HIGH);

  pinMode(SIM_STATUS_BFD, INPUT);
  productionDelay(1000);
}

static bool powerOnSimCom() {
  Serial.println(F("PowerOnSimCom"));
  delay(50);
  digitalWrite(SIM_PWRKEY, LOW);
  productionDelay(1050);
  digitalWrite(SIM_PWRKEY, HIGH);
  productionDelay(4000);

  for (uint8_t check = 0; check < 10; ++check) {
    if (isSimComOn()) {
      return true;
    }
    productionDelay(500);
  }

  return false;
}

static bool tryPowerOnSimCom(uint8_t tries) {
  Serial.println(F("Try to power on SimCom:"));
  Serial.print(F("SIM_STATUS_BFD: "));
  Serial.println(isSimComOn() ? F("HIGH") : F("LOW"));

  if (isSimComOn()) {
    Serial.println(F("SimCom is already on"));
    Serial.println(F("succeed."));
    return true;
  }

  for (uint8_t attempt = 0; attempt < tries; ++attempt) {
    if (powerOnSimCom()) {
      Serial.println(F("succeed."));
      return true;
    }
  }

  return false;
}

static bool hardResetSimCom() {
  Serial.println(F("Reset SIM7000"));
  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, LOW);
  productionDelay(2000);
  digitalWrite(SIM_RESET, HIGH);
  productionDelay(4000);

  for (uint8_t check = 0; check < 10; ++check) {
    if (isSimComOn()) {
      return true;
    }
    productionDelay(500);
  }

  return false;
}

static bool serialBeginAsProduction() {
  pinMode(SIM_TX_BFD, OUTPUT);
  digitalWrite(SIM_TX_BFD, HIGH);
  pinMode(SIM_RX_BFD, INPUT_PULLUP);
  MODEM.begin(MODEM_BAUD);
  productionDelay(1000);

  Serial.println(F("== negotiate modem baud rate =="));
  for (uint8_t attempt = 1; attempt <= 16; ++attempt) {
    Serial.print(F("AT attempt "));
    Serial.print(attempt);
    Serial.println(F("/16"));

    if (sendAT("AT", 1000UL) == AT_OK) {
      Serial.println(F("SIM7000 communication established at 9600 baud."));

      for (uint8_t echoAttempt = 0; echoAttempt < 4; ++echoAttempt) {
        if (sendAT("ATE0", 1000UL) == AT_OK) {
          return true;
        }
      }
      return false;
    }
  }

  return false;
}

static bool startModemAsProduction() {
  // The production logToWeb() starts Serial1 before trying PWRKEY.
  MODEM.begin(MODEM_BAUD);

  bool resetBeforeAttempt = false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    Serial.println();
    Serial.print(F("Production startup attempt "));
    Serial.print(attempt + 1);
    Serial.println(F("/3"));

    if (resetBeforeAttempt) {
      resetBeforeAttempt = false;
      Serial.println(F("Cannot communicate with SimCom; resetting."));
      hardResetSimCom();
    }

    if (!tryPowerOnSimCom(3)) {
      resetBeforeAttempt = true;
      continue;
    }

    if (!serialBeginAsProduction()) {
      resetBeforeAttempt = true;
      continue;
    }

    return true;
  }

  return false;
}


static bool runRequested = false;
static bool must(const char *cmd, uint32_t ms = 5000UL) {
  AtResult r = sendAT(cmd, ms);
  if (r == AT_OK) return true;
  Serial.println(F("TEST STOPPED: command failed. No further automatic commands."));
  if (r == AT_TIMEOUT) Serial.println(F("PENDING COMMAND: reset/resynchronise before further AT commands."));
  return false;
}
static void connectTest() {
  if (!must("AT+CMEE=2") || !must("AT+GMR")) return;
  if (!must("AT+CFUN=1",20000UL)) return;
  delay(5000);
  if (!must("AT+CPIN?") || !must("AT+COPS=2",150000UL)) return;
  if (!must("AT+CFUN=0",20000UL) || !must("AT+CNMP=38") ||
      !must("AT+CMNB=2") || !must("AT+CBANDCFG=\"NB-IOT\",28") ||
      !must("AT+CGDCONT=1,\"IP\",\"telstra.iot\"")) return;
  if (!must("AT+CNMP?") || !must("AT+CMNB?") ||
      !must("AT+CBANDCFG?") || !must("AT+CGDCONT?")) return;
  // Query reporting mode remains 0 so query responses cannot be confused with URCs.
  if (!must("AT+CFUN=1",20000UL)) return;
  delay(5000);
  if (!must("AT+CPIN?") || !must("AT+CEREG=0") || !must("AT+COPS=0",300000UL)) return;
  for (uint8_t i=0; i<40; ++i) {
    if (!must("AT+CEREG?")) return;
    int8_t state = lastCeregState;
    if (!must("AT+CPSI?") || !must("AT+COPS?") ||
        !must("AT+CSQ") || !must("AT+CGATT?")) return;
    if (state==1 || state==5) {
      Serial.println(F("EPS REGISTERED. Verify operator and NB-IoT in CPSI/COPS above."));
      if (!must("AT+CGNAPN") || !must("AT+CGPADDR=1")) return;
      if (!must("AT+CNACT=1,\"telstra.iot\"",150000UL)) return;
      delay(3000);
      if (!must("AT+CNACT?")) return;
      Serial.println(F("Registration test ended. No application data transfer tested."));
      return;
    }
    if (state==3) {
      must("AT+CEER");
      Serial.println(F("REGISTRATION DENIED: cause not established; inspect modem/provider logs."));
      return;
    }
    delay(15000);
  }
  must("AT+CEER");
  Serial.println(F("No registration within polling budget; inconclusive."));
}
void setup() {
  Serial.begin(9600);
  initialiseProductionPins();
  if (!startModemAsProduction()) {
    Serial.println(F("STARTUP FAILED")); return;
  }
  Serial.println(F("NB-IOT TEST READY: SEND !"));
}
void loop() {
  if (Serial.available()) {
    char c=Serial.read();
    if (!runRequested && c=='!') {
      runRequested=true;
      connectTest();
      Serial.println(F("TEST COMPLETE"));
    } else MODEM.write(c);
  }
  while (MODEM.available()) Serial.write(MODEM.read());
}
