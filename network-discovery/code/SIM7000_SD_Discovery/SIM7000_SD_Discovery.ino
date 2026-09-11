/*
  SIM7000G STAGE 1: BATTERY-POWERED DISCOVERY WITH SD LOGGING

  Board:          BoSL Board 0.5.x
  Serial Monitor: 9600 baud

  Purpose:
    - Discover visible LTE-M operators.
    - Discover visible NB-IoT operators.
    - Do not configure an APN.
    - Do not force a service provider or MCC-MNC.
    - Do not attempt an application data connection.

  Important:
    - Connect the LTE antenna before starting.
    - A SIM with +CPIN: READY is recommended. The APN is irrelevant here.
    - Each band is scanned separately with a bounded timeout.
    - Insert SD card and SIM before power-up; no USB required.
    - Return the Rxxxx.LOG, Rxxxx.CSV and Rxxxx.TXT files.
    - Wait for the final marker before removing SD (or disconnect battery first).

  +COPS tuple:
    (status,"long name","short name","MCCMNC",access technology)

  status: 0 unknown, 1 available, 2 current, 3 forbidden
  access technology on SIM7000G: 7 LTE-M, 9 NB-IoT
*/

#include <Arduino.h>
#include <avr/power.h>
#include <SD.h>

#define MODEM Serial1

static const uint32_t USB_BAUD = 9600UL;
static const uint32_t MODEM_BAUD = 9600UL;
static const uint32_t SHORT_TIMEOUT_MS = 5000UL;
// Time budgets are diagnostic limits, NOT proof that a band is empty.
static const uint32_t COPS_SCAN_TIMEOUT_MS = 180000UL;
#ifndef DISCOVERY_AU_VALIDATION
#define DISCOVERY_AU_VALIDATION 0
#endif

// Stage 1 tests every SIM7000G-supported band. To shorten a regional test,
// remove bands that the local operators do not use.
static const uint8_t CAT_M_BANDS[] =
    {1, 2, 3, 4, 5, 8, 12, 13, 18, 19, 20, 26, 28, 39};
static const uint8_t NB_IOT_BANDS[] =
    {1, 2, 3, 5, 8, 12, 13, 17, 18, 19, 20, 26, 28};

// Confirmed band lists for the tested SIM7000G firmware.
static const char CAT_M_ALL_BANDS[] =
    "AT+CBANDCFG=\"CAT-M\",1,2,3,4,5,8,12,13,18,19,20,26,28,39";
static const char NB_IOT_ALL_BANDS[] =
    "AT+CBANDCFG=\"NB-IOT\",1,2,3,5,8,12,13,17,18,19,20,26,28";


/*
  SD logging uses SD.begin(SD_CS), matching the production BoSL sketch.
  Each boot reserves an unused 8.3 filename set; existing runs are retained.
*/
static File rawLog;
static bool sdFailed = false, loggingReady = false, writeSummary = false;
static bool finishedRun = false, modemOff = false;
static File summaryLog;
static char rawName[13], csvName[13], summaryName[13];

class RunLogger : public Print {
 public:
  using Print::write;
  size_t write(uint8_t c) override {
    Serial.write(c); // Hardware UART works even without USB connected.
    if (loggingReady && !sdFailed) {
      buffer[used++] = c;
      if (used == sizeof(buffer)) drain();
      if (writeSummary && summaryLog.write(c) != 1) sdFailed = true;
    }
    return 1;
  }
  void drain() {
    if (used && loggingReady && !sdFailed) {
      if (rawLog.write(buffer, used) != used) sdFailed = true;
    }
    used = 0;
  }
  void checkpoint() {
    drain();
    if (loggingReady) {
      rawLog.flush();
      if (rawLog.getWriteError()) sdFailed = true;
    }
  }
 private:
  uint8_t buffer[256];
  uint16_t used = 0;
};
static RunLogger logger;

static bool beginRunFiles() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  if (!SD.begin(SD_CS)) return false;
  for (uint16_t n=1; n<10000; ++n) {
    snprintf(rawName, sizeof(rawName), "R%04u.LOG", n);
    snprintf(csvName, sizeof(csvName), "R%04u.CSV", n);
    snprintf(summaryName, sizeof(summaryName), "R%04u.TXT", n);
    if (SD.exists(rawName) || SD.exists(csvName) || SD.exists(summaryName)) continue;
    rawLog = SD.open(rawName, FILE_WRITE);
    if (!rawLog) return false;
    File csv = SD.open(csvName, FILE_WRITE);
    if (!csv) { rawLog.close(); return false; }
    csv.println(F("record,scan,rat,band,result,seconds,tuples,plmn,status,act,operator,candidates,mismatches,classification,error"));
    csv.flush();
    const bool csvOK = !csv.getWriteError();
    csv.close();
    File summary = SD.open(summaryName, FILE_WRITE);
    if (!summary) { rawLog.close(); return false; }
    summary.println(F("RUN STARTED. No final marker means interrupted/incomplete."));
    summary.flush();
    const bool sumOK = !summary.getWriteError();
    summary.close();
    if (!csvOK || !sumOK) { rawLog.close(); return false; }
    loggingReady = true;
    logger.println(F("SD LOGGING STARTED"));
    logger.println(rawName); logger.println(csvName); logger.println(summaryName);
    logger.checkpoint();
    return !sdFailed;
  }
  return false;
}

extern volatile unsigned long timer0_millis;

enum AtResult : uint8_t {
  AT_OK,
  AT_ERROR,
  AT_TIMEOUT
};
static AtResult sendAT(const char *command, uint32_t timeoutMs);

struct ScanRow {
  uint8_t rat, band, outcome;
  uint16_t seconds, tuples, candidates, mismatches;
  char error[48]; // Full, untruncated response remains in the raw LOG.
};
struct OperatorRow { uint8_t scan, status, act; char plmn[7]; char name[25]; };
static ScanRow scans[27];
static OperatorRow operatorsFound[40];
static uint8_t scanCount = 0, operatorCount = 0;
static bool summaryOverflow = false, parserIssue = false;
static bool simReady = false, copsDeregistered = false, offline = false;
static bool commandPending = false;
static bool allScansAttempted = false;
static char lastAtError[48] = "";
static char firmware[48] = "unknown";
static char failure[80] = "none";
static char expectedReply[80] = "";
static bool expectedSeen = false;
// outcomes: 0 incomplete, 1 complete, 2 timeout, 3 modem error, 4 setup error
static const char *outcomeName(uint8_t code) {
  switch(code) {
    case 1: return "COMPLETE";
    case 2: return "TIMEOUT/UNKNOWN";
    case 3: return "MODEM ERROR";
    case 4: return "SETUP ERROR";
    default: return "NOT COMPLETED";
  }
}

// Consume one bounded tuple, never the entire potentially very long COPS line.
static void saveTuple(char *tuple) {
  if (!scanCount || !strchr(tuple, '"')) return; // trailing capability tuples
  char fields[5][65] = {{0}};
  uint8_t field = 0, pos = 0;
  bool quoted = false;
  for (char *p = tuple; *p; ++p) {
    if (*p == '"') { quoted = !quoted; continue; }
    if (*p == ',' && !quoted) {
      if (++field >= 5) { parserIssue = true; return; }
      pos = 0;
    } else if (pos < 64) fields[field][pos++] = *p;
    else { parserIssue = true; return; }
  }
  if (quoted || field != 4) { parserIssue = true; return; }
  unsigned int status, act;
  if (sscanf(fields[0], "%u", &status) != 1 || status > 3 ||
      sscanf(fields[4], "%u", &act) != 1 || act > 255) {
    parserIssue = true; return;
  }
  const size_t digits = strlen(fields[3]);
  if (digits < 5 || digits > 6 || strspn(fields[3], "0123456789") != digits) {
    parserIssue = true; return;
  }
  ScanRow &row = scans[scanCount - 1];
  ++row.tuples;
  if (act == row.rat) ++row.candidates;
  else ++row.mismatches;
  if (operatorCount >= 40) { summaryOverflow = true; return; }
  OperatorRow &o = operatorsFound[operatorCount++];
  o.scan = scanCount - 1; o.status = status; o.act = act;
  strncpy(o.plmn, fields[3], sizeof(o.plmn)-1);
  strncpy(o.name, fields[1], sizeof(o.name)-1);
}

static void heading(const __FlashStringHelper *text) {
  logger.println();
  logger.println(F("============================================================"));
  logger.println(text);
  logger.println(F("============================================================"));
}

static void productionDelay(uint32_t durationMs) {
  logger.checkpoint();
  Serial.flush();

  const uint32_t slowedDelayMs = durationMs / 64UL;
  clock_prescale_set(clock_div_64);
  delay(slowedDelayMs);
  clock_prescale_set(clock_div_1);

  cli();
  timer0_millis += 63UL * slowedDelayMs;
  sei();

  delay(durationMs - 64UL * slowedDelayMs);
}

static void flushOldModemData() {
  const uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 150UL) {
    while (MODEM.available()) {
      logger.write(MODEM.read());
    }
  }
}

/*
  Stream responses instead of storing them in a 254-byte global buffer.
  A complete +COPS response may be much longer than 254 bytes.
*/
static AtResult sendAT(const char *command, uint32_t timeoutMs) {
  flushOldModemData();

  const bool scanning = strcmp(command, "AT+COPS=?") == 0;
  const bool pinQuery = strcmp(command, "AT+CPIN?") == 0;
  const bool copsQuery = strcmp(command, "AT+COPS?") == 0;
  if (pinQuery) simReady = false;
  if (copsQuery) copsDeregistered = false;
  expectedSeen = false;
  lastAtError[0] = '\0';
  char tuple[192];
  uint16_t tupleLength = 0;
  bool inTuple = false, tupleQuoted = false, tupleOverflow = false;

  logger.println();
  logger.print(F(">>> "));
  logger.println(command);

  MODEM.print(command);
  MODEM.print('\r');

  char line[96];
  size_t length = 0;
  bool oversizedLine = false;
  AtResult result = AT_TIMEOUT;
  bool finished = false;

  const uint32_t started = millis();
  uint32_t lastProgress = started;

  while (!finished && (uint32_t)(millis() - started) < timeoutMs) {
    while (MODEM.available()) {
      const char incoming = (char)MODEM.read();
      logger.write(incoming);

      if (scanning) {
        if (!inTuple && incoming == '(') {
          inTuple = true; tupleLength = 0; tupleQuoted = false; tupleOverflow = false;
        } else if (inTuple) {
          if (incoming == '"') tupleQuoted = !tupleQuoted;
          if (incoming == ')' && !tupleQuoted) {
            tuple[tupleLength] = '\0';
            if (!tupleOverflow) saveTuple(tuple); else parserIssue = true;
            inTuple = false;
          } else if (tupleLength < sizeof(tuple)-1) tuple[tupleLength++] = incoming;
          else tupleOverflow = true;
        }
      }

      if (incoming == '\r') {
        continue;
      }

      if (incoming == '\n') {
        if (!oversizedLine) {
          line[length] = '\0';
          if (expectedReply[0] && strcmp(line, expectedReply) == 0) expectedSeen = true;
          if (pinQuery && strcmp(line, "+CPIN: READY") == 0) simReady = true;
          if (copsQuery && strcmp(line, "+COPS: 2") == 0) copsDeregistered = true;
          if (strcmp(command, "AT+GMR") == 0 && length &&
              strcmp(line, "OK") != 0 && strncmp(line, "AT", 2) != 0) {
            strncpy(firmware, line, sizeof(firmware)-1);
          }

          if (strcmp(line, "OK") == 0) {
            result = AT_OK;
            finished = true;
          } else if (strcmp(line, "ERROR") == 0 ||
                     strstr(line, "+CME ERROR:") != NULL ||
                     strstr(line, "+CMS ERROR:") != NULL) {
            result = AT_ERROR;
            strncpy(lastAtError, line, sizeof(lastAtError)-1);
            lastAtError[sizeof(lastAtError)-1] = '\0';
            finished = true;
          }
        }

        length = 0;
        oversizedLine = false;
      } else if (length < sizeof(line) - 1) {
        line[length++] = incoming;
      } else {
        // Continue forwarding the line without overflowing AVR RAM.
        oversizedLine = true;
      }
    }

    if (timeoutMs >= 60000UL &&
        (uint32_t)(millis() - lastProgress) >= 30000UL) {
      logger.print(F("\r\n[host waiting; modem progress unknown: "));
      logger.print((uint32_t)(millis() - started) / 1000UL);
      logger.println(F(" seconds elapsed]"));
      logger.checkpoint();
      lastProgress = millis();
    }

    delay(1);
  }

  logger.println();
  logger.print(F("<<< "));
  if (result == AT_OK) {
    logger.println(F("OK"));
  } else if (result == AT_ERROR) {
    logger.println(F("MODEM ERROR"));
  } else {
    logger.print(F("TIMEOUT after "));
    logger.print(timeoutMs / 1000UL);
    logger.println(F("s; result INCONCLUSIVE"));
  }

  commandPending = result == AT_TIMEOUT;
  if (scanning && inTuple) parserIssue = true;

  logger.checkpoint();
  return result;
}

static bool isSimComOn() {
  return digitalRead(SIM_STATUS_BFD);
}

static void initialiseProductionPins() {
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
  logger.println(F("PowerOnSimCom"));
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
  logger.println(F("Try to power on SimCom:"));
  logger.print(F("SIM_STATUS_BFD: "));
  logger.println(isSimComOn() ? F("HIGH") : F("LOW"));

  if (isSimComOn()) {
    logger.println(F("SimCom is already on"));
    logger.println(F("succeed."));
    return true;
  }

  for (uint8_t attempt = 0; attempt < tries; ++attempt) {
    if (powerOnSimCom()) {
      logger.println(F("succeed."));
      return true;
    }
  }

  return false;
}

static bool hardResetSimCom() {
  logger.println(F("Reset SIM7000"));
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

  logger.println(F("== negotiate modem baud rate =="));
  for (uint8_t attempt = 1; attempt <= 16; ++attempt) {
    logger.print(F("AT attempt "));
    logger.print(attempt);
    logger.println(F("/16"));

    if (sendAT("AT", 1000UL) == AT_OK) {
      logger.println(F("SIM7000 communication established at 9600 baud."));
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
  // Same order as production logToWeb(): start Serial1 before PWRKEY checks.
  MODEM.begin(MODEM_BAUD);

  bool resetBeforeAttempt = false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    logger.println();
    logger.print(F("Production startup attempt "));
    logger.print(attempt + 1);
    logger.println(F("/3"));

    if (resetBeforeAttempt) {
      resetBeforeAttempt = false;
      logger.println(F("Cannot communicate with SimCom; resetting."));
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

static bool requireAT(const char *cmd, uint32_t timeout) {
  if (sendAT(cmd, timeout) == AT_OK) return true;
  snprintf(failure, sizeof(failure), "Command failed: %.52s", cmd);
  return false;
}

static bool verifyAT(const char *cmd, const char *expected) {
  strncpy(expectedReply, expected, sizeof(expectedReply)-1);
  const bool ok = requireAT(cmd, SHORT_TIMEOUT_MS) && expectedSeen;
  expectedReply[0] = '\0';
  if (!ok && !commandPending)
    snprintf(failure, sizeof(failure), "Readback mismatch: %.48s", cmd);
  return ok;
}

static bool recoverPendingCommand() {
  // A host timeout does not cancel a modem command. Resynchronise first.
  if (!hardResetSimCom() || !serialBeginAsProduction()) return false;
  commandPending = false;
  return requireAT("AT+CMEE=2", SHORT_TIMEOUT_MS);
}

static bool waitForSim() {
  for (uint8_t i=0; i<10; ++i) {
    const AtResult r = sendAT("AT+CPIN?", SHORT_TIMEOUT_MS);
    if (r == AT_TIMEOUT) break;
    if (r == AT_OK && simReady) return true;
    delay(1000); // keep the UART clock running during modem initialisation
  }
  strncpy(failure, "SIM not READY; see CPIN response in full log", sizeof(failure)-1);
  return false;
}

static bool deregister() {
  if (!requireAT("AT+COPS=2", 150000UL)) return false;
  if (!requireAT("AT+COPS?", SHORT_TIMEOUT_MS)) return false;
  if (copsDeregistered) return true;
  strncpy(failure, "COPS mode 2 could not be verified", sizeof(failure)-1);
  return false;
}

static bool runBandOperatorScan(uint8_t rat, uint8_t band) {
  ScanRow &row = scans[scanCount++];
  row.rat = rat; row.band = band; row.outcome = 4;
  heading(F("VISIBLE OPERATOR BAND SCAN"));
  logger.print(rat == 7 ? F("LTE-M") : F("NB-IoT"));
  logger.print(F(" BAND ")); logger.println(band);
  char bandCommand[42];
  snprintf(bandCommand, sizeof(bandCommand),
           "AT+CBANDCFG=\"%s\",%u", rat == 7 ? "CAT-M" : "NB-IOT", band);

  if (!requireAT("AT+CFUN=0", 20000UL) ||
      !requireAT("AT+CNMP=38", SHORT_TIMEOUT_MS) ||
      !requireAT(rat == 7 ? "AT+CMNB=1" : "AT+CMNB=2", SHORT_TIMEOUT_MS) ||
      !requireAT(bandCommand, SHORT_TIMEOUT_MS) ||
      !verifyAT("AT+CNMP?", "+CNMP: 38") ||
      !verifyAT("AT+CMNB?", rat==7 ? "+CMNB: 1" : "+CMNB: 2")) return false;
  char expectedBand[42];
  snprintf(expectedBand, sizeof(expectedBand), "+CBANDCFG: \"%s\",%u",
           rat==7 ? "CAT-M" : "NB-IOT", band);
  if (!verifyAT("AT+CBANDCFG?", expectedBand) ||
      !requireAT("AT+CFUN=1", 20000UL)) return false;
  delay(3000);
  if (!waitForSim() || !deregister()) return false;

  // A timeout is an intentionally limited first pass, never a negative finding.
  const uint32_t started = millis();
  const AtResult result = sendAT("AT+COPS=?", COPS_SCAN_TIMEOUT_MS);
  row.seconds = (millis()-started)/1000UL;
  row.outcome = result == AT_OK ? 1 : result == AT_TIMEOUT ? 2 : 3;
  if (result == AT_ERROR) {
    strncpy(row.error, lastAtError, sizeof(row.error)-1);
  }
  if (result == AT_TIMEOUT && !recoverPendingCommand()) {
    strncpy(failure, "Modem recovery failed after scan timeout", sizeof(failure)-1);
    return false;
  }
  return true;
}

static void printSummary() {
  heading(F("DISCOVERY SD SUMMARY v4"));
  logger.println(DISCOVERY_AU_VALIDATION ? F("PROFILE: AU / M1 B28; NB B5+B28") :
                                          F("PROFILE: HK DISCOVERY / GLOBAL CANDIDATE BANDS"));
  logger.print(F("Run files: ")); logger.print(rawName); logger.print(' ');
  logger.print(csvName); logger.print(' '); logger.println(summaryName);
  logger.print(F("Run state: ")); logger.println(finishedRun ? F("FINISHED") : F("CHECKPOINT / INCOMPLETE"));
  logger.print(F("SD write status: ")); logger.println(sdFailed ? F("ERROR") : F("OK"));
  logger.print(F("Modem power off verified: ")); logger.println(modemOff ? F("YES") : F("NO"));
  logger.print(F("Firmware: ")); logger.println(firmware);
  logger.print(F("SIM READY observed: ")); logger.println(simReady ? F("YES") : F("NO"));
  logger.print(F("Radio OFF verified: ")); logger.println(offline ? F("YES") : F("NO / CHECK LOG"));
  logger.print(F("Fatal/cleanup detail (not per-band errors): ")); logger.println(failure);
  uint16_t candidates = 0, mismatches = 0;
  uint8_t complete = 0, timeouts = 0, errors = 0;
  for (uint8_t i=0; i<scanCount; ++i) {
    candidates += scans[i].candidates;
    mismatches += scans[i].mismatches;
    if (scans[i].outcome == 1) ++complete;
    else if (scans[i].outcome == 2) ++timeouts;
    else ++errors;
  }
  logger.print(F("Assessment: "));
  if (!finishedRun) logger.println(F("IN PROGRESS / INCOMPLETE"));
  else if (!allScansAttempted) logger.println(F("STOPPED EARLY / INCOMPLETE"));
  else if (timeouts || errors || mismatches || parserIssue || summaryOverflow ||
           sdFailed || !modemOff || !offline || strcmp(failure,"none") != 0)
    logger.println(F("FINISHED WITH ERRORS / INCONCLUSIVE TESTS"));
  else logger.println(F("SCAN PASS FINISHED; ACCESS NOT TESTED"));
  logger.print(F("Scans: ")); logger.print(scanCount);
  logger.print(F("; OK: ")); logger.print(complete);
  logger.print(F("; timeout: ")); logger.print(timeouts);
  logger.print(F("; error/incomplete: ")); logger.println(errors);
  logger.print(F("RAT-matching candidate reports: ")); logger.println(candidates);
  logger.print(F("RAT-mismatched reports (NOT candidates): ")); logger.println(mismatches);
  logger.println(F("RAT BAND RESULT SEC TUPLES MATCH MISMATCH"));
  for (uint8_t i=0; i<scanCount; ++i) {
    const ScanRow &r=scans[i];
    logger.print(r.rat==7 ? F("M1  ") : F("NB  "));
    logger.print(r.band); logger.print(' ');
    logger.print(outcomeName(r.outcome)); logger.print(' ');
    logger.print(r.seconds); logger.print(' '); logger.print(r.tuples);
    logger.print(' '); logger.print(r.candidates);
    logger.print(' '); logger.println(r.mismatches);
    if (r.error[0]) { logger.print(F("  Error/detail: ")); logger.println(r.error); }
  }
  logger.println(F("M1=LTE-M; NB=NB-IoT. MATCH=reported AcT matches scan RAT."));
  logger.println(F("Counts are reports, not unique networks or successful access."));
  logger.println(F("TIMEOUT is UNKNOWN, not proof of absent coverage."));
  logger.println(F("COMPLETE means AT returned OK, not successful registration."));
  logger.println(F("Mismatches and partial scans require follow-up; see full LOG."));
  logger.println(F("APN untouched. No SIM entitlement/data test performed."));
  if (summaryOverflow || parserIssue)
    logger.println(F("SUMMARY INCOMPLETE: send full log (overflow/parse issue)."));
  logger.println(F("Press S to repeat summary. Save full log as well."));
  // Limit each operator page to ten rows for screenshots.
  for (uint8_t base=0; base<operatorCount; base+=10) {
    heading(F("SCREENSHOT THIS: CLASSIFIED OPERATOR REPORTS"));
    logger.print(F("Page ")); logger.print(base/10+1);
    logger.print('/'); logger.println((operatorCount+9)/10);
    logger.println(F("SCAN-RAT BAND PLMN STATUS AcT CLASS OPERATOR"));
    for (uint8_t i=base; i<operatorCount && i<base+10; ++i) {
      const OperatorRow &o=operatorsFound[i];
      const ScanRow &r=scans[o.scan];
      logger.print(r.rat==7 ? F("M1 ") : F("NB "));
      logger.print(r.band); logger.print(' ');
      logger.print(o.plmn); logger.print(' ');
      logger.print(o.status); logger.print(' ');
      logger.print(o.act); logger.print(' ');
      logger.print(o.act != r.rat ? F("MISMATCH") :
                   r.outcome == 1 ? F("CANDIDATE") : F("PARTIAL"));
      logger.print(' '); logger.println(o.name);
    }
    logger.println(F("STATUS: 0 unknown; 1 available; 2 current; 3 forbidden."));
    logger.println(F("AcT: 7=M1; 9=NB. Visibility does not guarantee SIM access."));
    logger.println(F("Band is the configured scan band; check RAT against AcT."));
  }
}


static void csvQuoted(File &file, const char *value) {
  file.write('"');
  for (const char *p=value; *p; ++p) {
    if (*p=='"') file.write('"');
    file.write(*p);
  }
  file.write('"');
}

static void saveCheckpoint(bool finalCheckpoint) {
  if (!loggingReady || sdFailed) return;
  logger.checkpoint();
  File csv = SD.open(csvName, FILE_WRITE);
  if (!csv) { sdFailed = true; return; }
  // Persist the last scan once, after it completes or fails during setup.
  static uint8_t savedScans = 0;
  while (savedScans < scanCount) {
    const uint8_t index = savedScans++;
    const ScanRow &r = scans[index];
    csv.print(F("SCAN,")); csv.print(index+1); csv.print(',');
    csv.print(r.rat==7 ? F("LTE-M") : F("NB-IoT")); csv.print(',');
    csv.print(r.band); csv.print(','); csv.print(outcomeName(r.outcome));
    csv.print(','); csv.print(r.seconds); csv.print(','); csv.print(r.tuples);
    csv.print(F(",,,,,")); csv.print(r.candidates); csv.print(',');
    csv.print(r.mismatches); csv.print(F(",,"));
    csvQuoted(csv, r.error); csv.println();
    for (uint8_t j=0; j<operatorCount; ++j) {
      const OperatorRow &o = operatorsFound[j];
      if (o.scan != index) continue;
      csv.print(F("OPERATOR,")); csv.print(index+1); csv.print(',');
      csv.print(r.rat==7 ? F("LTE-M") : F("NB-IoT")); csv.print(',');
      csv.print(r.band); csv.print(','); csv.print(outcomeName(r.outcome));
      csv.print(F(",,,")); csv.print(o.plmn); csv.print(',');
      csv.print(o.status); csv.print(','); csv.print(o.act); csv.print(',');
      csvQuoted(csv, o.name); csv.print(F(",,,"));
      csv.print(o.act != r.rat ? F("RAT_MISMATCH") :
                r.outcome == 1 ? F("CANDIDATE") : F("PARTIAL_CANDIDATE"));
      csv.print(','); csvQuoted(csv, r.error); csv.println();
    }
  }
  if (finalCheckpoint) csv.println(F("END,,,,,,,,,,,,,,"));
  csv.flush();
  if (csv.getWriteError()) sdFailed = true;
  csv.close();
  summaryLog = SD.open(summaryName, FILE_WRITE);
  if (!summaryLog) { sdFailed = true; return; }
  writeSummary = true;
  printSummary();
  logger.println(finalCheckpoint ? F("FINAL SUMMARY END") : F("CHECKPOINT END"));
  writeSummary = false;
  summaryLog.flush();
  if (summaryLog.getWriteError()) sdFailed = true;
  summaryLog.close();
  logger.checkpoint();
}

static bool scanAndSave(uint8_t rat, uint8_t band) {
  if (sdFailed) {
    strncpy(failure, "SD write failed; scans stopped", sizeof(failure)-1);
    return false;
  }
  const bool ok = runBandOperatorScan(rat, band);
  if (!ok && scans[scanCount-1].outcome == 4) {
    ScanRow &row = scans[scanCount-1];
    strncpy(row.error, lastAtError[0] ? lastAtError : failure, sizeof(row.error)-1);
  }
  saveCheckpoint(false);
  return ok && !sdFailed;
}

static void shutDownModem() {
  // CFUN=0 disables RF but does not power off the modem.
  // CPOWD's final response is NORMAL POWER DOWN, not necessarily OK.
  if (!isSimComOn()) { modemOff = true; return; }
  logger.println(F("Requesting modem shutdown: AT+CPOWD=1"));
  logger.checkpoint();
  MODEM.print(F("AT+CPOWD=1\r"));
  const uint32_t start = millis();
  while (millis()-start < 30000UL) {
    while (MODEM.available()) logger.write(MODEM.read());
    if (!isSimComOn()) { modemOff = true; break; }
    delay(1);
  }
  logger.checkpoint();
  // If status stays high, record it; do not blindly toggle PWRKEY.
}

static bool performDiscovery() {
  if (!startModemAsProduction()) {
    strncpy(failure, "No modem UART response", sizeof(failure)-1);
    return false;
  }
  if (!requireAT("AT+CMEE=2", SHORT_TIMEOUT_MS) ||
      !requireAT("AT+GMR", SHORT_TIMEOUT_MS)) return false;
  if (!requireAT("AT+CFUN=1", 20000UL)) return false;
  delay(3000);
  if (!waitForSim() || !deregister()) return false;
  if (!requireAT("AT+CBANDCFG=?", SHORT_TIMEOUT_MS) ||
      !requireAT("AT+CNBS?", SHORT_TIMEOUT_MS) ||
      !verifyAT("AT+MCELLLOCK?", "+MCELLLOCK: 0") ||
      !verifyAT("AT+NCELLLOCK?", "+NCELLLOCK: 0")) return false;

#if DISCOVERY_AU_VALIDATION
  if (!scanAndSave(7,28)) return false;
  if (!scanAndSave(9,5)) return false;
  if (!scanAndSave(9,28)) return false;
#else
  for (uint8_t i=0; i<sizeof(CAT_M_BANDS); ++i)
    if (!scanAndSave(7,CAT_M_BANDS[i])) return false;
  for (uint8_t i=0; i<sizeof(NB_IOT_BANDS); ++i)
    if (!scanAndSave(9,NB_IOT_BANDS[i])) return false;
#endif
  return true;
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(1500);
  if (!beginRunFiles()) {
    logger.println(F("SD INIT/FILE ERROR: scan NOT started. Check FAT16/FAT32 card."));
    sdFailed = true;
    return;
  }
  heading(F("SIM7000G BATTERY + SD DISCOVERY v4"));
  logger.println(F("Deregisters before each scan; APN is not modified."));
  logger.println(F("3-minute first pass per band; timeouts remain inconclusive."));
  logger.println(F("Full profile can take over 90 minutes. Leave power connected."));
  initialiseProductionPins();
  allScansAttempted = performDiscovery();

  // Always attempt cleanup and always print a summary, including on errors.
  bool canClean = !commandPending || recoverPendingCommand();
  if (canClean) {
    offline = sendAT("AT+CFUN=0",20000UL) == AT_OK &&
              verifyAT("AT+CFUN?", "+CFUN: 0");
    if (offline) {
      // Explicit baseline, not restoration of unknown pre-test settings.
      const bool baselineOK = requireAT("AT+CNMP=38",SHORT_TIMEOUT_MS) &&
        requireAT("AT+CMNB=3",SHORT_TIMEOUT_MS) &&
        requireAT(CAT_M_ALL_BANDS,SHORT_TIMEOUT_MS) &&
        requireAT(NB_IOT_ALL_BANDS,SHORT_TIMEOUT_MS);
      if (!baselineOK) logger.println(F("Baseline settings incomplete; see summary."));
    }
  }
  shutDownModem();
  finishedRun = true;
  saveCheckpoint(true);
  logger.println(sdFailed ? F("TEST STOPPED: SD ERROR") : F("TEST COMPLETE; SD FILES CLOSED"));
  logger.checkpoint();
  rawLog.close();
  loggingReady = false;
}

void loop() {
  while (Serial.available()) {
    const char c=Serial.read();
    if (c=='s' || c=='S') printSummary();
  }
}
