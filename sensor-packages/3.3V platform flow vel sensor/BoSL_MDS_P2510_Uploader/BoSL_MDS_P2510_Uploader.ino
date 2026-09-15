// File: BoSL_MDS_P2510_Uploader.ino
// MDS-01: replaces the original turbidity and depth/EC sensors.
// Sensor: VPP_SW -> MT3608 -> regulated 12 V; common ground.
// Auto-direction RS485 converter: EN_SWB 3.3 V, TXD2/RXD2, A/B.
// Serial2: Modbus RTU 9600 8N1, address 1. Serial1: existing SIM7000.
// PWR-06: EN_SWVPP and EN_SWB on per measurement, off before SD/network work.
// MT3608 input must use VPP_SW, not the always-on VBAT terminal.
// WEB-01: LOG_TO_WEB is the only web-enable switch; A15 is not used.

/* this code requires arduino IDE > 2.3.2  */

// ********* Configurations for setup ************
#define SITE_DIR "testing"
#define SITE_ID "Eric_Testing"
#define APN "simbase"
#define MCCMNC "50501"
#define SD_LOG_ALL_DATA      true       // save raw sensor readings to SD card.
#define SD_LOG_FILE_ALL_DATA "flowraw.csv"  // file to save raw sensor readings (max 8.3 characters)
#define SD_LOG_FILE_WEB_DATA  "flowavg.csv" // file to save averaged sensor readings (max 8.3 characters)
#define SENSOR_STARTUP_MS 20000UL  // conservative allowance, not a manual spec
#ifndef LOG_TO_WEB
#define LOG_TO_WEB true // WEB-01: true enables web/time sync; false keeps SD-only logging
#endif
#define ALLOW_GSM false                 // most networks have disabled GSM
#define ALLOW_NBIOT false               // NB-IoT requires simcard support
#define ACCEPT_NON_ROAMING true         // set false to only accept roaming connections
#define LOOP_INTERVAL_MINUTES 2         // PWR-02: one measurement cycle per six minutes
#define AVERAGING_INTERVAL_MINUTES 2    // averaging and upload interval (minutes)
#define CONSTANT_LOGGING true           // if true, it will log to web every
                                        // AVERAGING_INTERVAL_MINUTES

// * Variable logging control * // only used when CONSTANT_LOGGING is false
#define MAX_HTTP_INTERVAL_MINUTES 12 // maximum upload interval (minutes)
/*  In variable logging, a log is triggered when any of the sensor readings change by 
 *  more than an absolute or relative threshold from the previous upload.
 *  The below define these absolute and relative thresholds.
 */
#define THRESHOLD_PERCENT 10.0    // relative threshold for sensor readings (%)
// MDS-03: thresholds use internal SI units; velocity threshold is configurable.
#define DEPTH_DIFF_THRESHOLD_M 0.010f
#define VELOCITY_DIFF_THRESHOLD_MPS 0.010f
//************************************************
//************************************************

// ********* Configurations for error handling ************
#define HTTP_FAILS_RESET_THRESHOLD       (240) //how many HTTP fails are needed to trigger a sim7000 reset
#define TRY_POWER_ON_MAX_RESETS          (3)   //max number of reset attempts when attempting to power on the sim7000

#define POWER_ON_FAILS_RESET_LIMIT_BEGIN (6)   //how many power on resets are needed to rate limit the reset attempts
#define POWER_ON_FAILS_RESET_LIMIT_RATE  (30)  //in the rate limit mode, this is the number of reset requests needed before a reset is actually performed.

#define NET_REG_FAILS_LIMIT_BEGIN (12)   //how many power on resets are needed to rate limit the reset attempts
#define NET_REG_FAILS_LIMIT_RATE  (120)  //in the rate limit mode, this is the number of reset requests needed before a reset is actually performed.

//*************************************************************
// Do NOT touch bellow code unless you know what you are doing.
//*************************************************************

// MDS-04: clean Modbus driver; no TX/RX dumps.
#include "BoSLFlow.h"
#include <MCP7940.h>
#include <SD.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// *** special macro ***
#define MAX_OPERATORS (8)
#define POWER_ON_TRY_TIMES 5
#define HTTP_UPLOAD_TRY_TIMES 3 // to fix the issues of simcom or bad signal.
// first time try power off simcom, second time try reset simcom, third time
// and forward only try power off simcom to avoid frequent reset

// MDS-05: Serial2, EN_SWB and EN_SWVPP belong exclusively to BoSLFlow.
#define simCom Serial1

// *** Constants ***
#define LOOP_INTERVAL_MILLIS (LOOP_INTERVAL_MINUTES * 60000UL) // sensor scan
                                                             // interval(ms)
#define BAUDRATE 9600 // talk to SIM7000 baud rate
#define CHARBUFF 254  // SIM7000 serial response buffer,longer than 255 will cause issues
#define SIM_RESET_MIN_INTERVAL_HOURS 3
#define MIN_NON_RESET_LOOP_COUNT (SIM_RESET_MIN_INTERVAL_HOURS * 60 / LOOP_INTERVAL_MINUTES)
#define LOOPS_COUNT_PER_AVERAGE (AVERAGING_INTERVAL_MINUTES / LOOP_INTERVAL_MINUTES)


/* CONFIGURATION VALIDATION */
// MDS-06: reject invalid averaging intervals.
static_assert(LOOP_INTERVAL_MINUTES > 0, "Scan interval must be positive");
static_assert(AVERAGING_INTERVAL_MINUTES >= LOOP_INTERVAL_MINUTES &&
              AVERAGING_INTERVAL_MINUTES % LOOP_INTERVAL_MINUTES == 0,
              "Average interval must be a positive multiple of scan interval");
static_assert(LOOPS_COUNT_PER_AVERAGE <= 65535UL, "Too many scans per average");
constexpr bool allowed_chars(char c) noexcept {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9');
}

constexpr bool is_8_3_array(const char* s, size_t len) noexcept {
    if (len == 0) {return false;}            // empty string not allowed
    size_t base = 0, ext = 0;
    bool seen_dot = false;

    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '.') {
            if (seen_dot) {return false;}    // more than one dot
            if (base == 0) {return false;}   // no empty base
            seen_dot = true;
            continue;
        }
        if (!allowed_chars(c)) {return false;}       // disallowed character
        if (!seen_dot) {
            if (++base > 8) {return false;} // base more than 8
        } else {
            if (++ext > 3) {return false;}  // ext more than 3
        }
    }

    if (seen_dot) {
      return (base >= 1 && ext >= 1); // extension required if dot present
    } else {
      return (base >= 1 && base <= 8);              // no dot -> base only (1..8)
    }
}

template<size_t N>
constexpr bool is_8_3(const char (&s)[N]) noexcept {
    return is_8_3_array(s, N - 1);
}

#define VALID_FILENAME(FILE_NAME) static_assert(is_8_3(FILE_NAME), FILE_NAME " must be an 8.3 filename");

VALID_FILENAME(SD_LOG_FILE_ALL_DATA);
VALID_FILENAME(SD_LOG_FILE_WEB_DATA);


/* END CONFIG VALIDATION */

// *** global variables ***
// atmega timer0_millis
extern volatile unsigned long timer0_millis;
// RTC
MCP7940_Class MCP7940;
struct DATETIME {
public:
  void init(int16_t yr, int16_t mt, int16_t dy, int16_t hr, int16_t mn, int16_t sc) {
    this->yr = yr;
    this->mt = mt;
    this->dy = dy;
    this->hr = hr;
    this->mn = mn;
    this->sc = sc;
  }
  int16_t yr;
  int16_t mt;
  int16_t dy;
  int16_t hr;
  int16_t mn;
  int16_t sc;
} g_datetime;
// SIM7000
char response[CHARBUFF]; // sim7000 serial response buffer
String dataStr;          // Transmit URL
char CBC[5];
char CSQ[5];
// MDS-07: SI readings with independent validity; never substitute zero on failure.
BoSLFlow::Sample flowSample;
double velocityVal = NAN;
double depthVal = NAN;
bool velocityValid = false;
bool depthValid = false;
uint16_t velocityValidCount = 0;
uint16_t depthValidCount = 0;

// SD card variables
File myFile;
unsigned long sdRecordIndex = 0;
unsigned long webRecordIndex = 0;
bool isSDSetupDone = false;
// SIM RESET control
uint32_t noResetLoopsCount = MIN_NON_RESET_LOOP_COUNT;

// *** logging control ***
// MDS-08: average valid samples only, separately for each measurement.
double velocitySum = 0;
double depthSum = 0;
double preVelocityAverage = NAN;
double preDepthAverage = NAN;
bool preVelocityValid = false;
bool preDepthValid = false;
bool haveUploadBaseline = false;
uint16_t velocityAverageCount = 0;
uint16_t depthAverageCount = 0;
uint16_t scanCounterForHttp = 0;
uint32_t noLoggingLoopCounter = 0;

// *** function prototypes ***
bool negoSimComBaudRate(int32_t baud = BAUDRATE);
bool tryPowerOnSimCom(int8_t tries = POWER_ON_TRY_TIMES);
bool sendATcmd(String ATcommand, char *expctAns, uint32_t timeout = 1000,
               int8_t tries = 5);

struct Operator {
  uint8_t status;
  char mccmnc[7];
  uint8_t netact;
  uint8_t rssi;
};

uint8_t operators_len = 0;
Operator operators[MAX_OPERATORS];


// *** function definitions ***
bool serialBegin() {
  pinMode(SIM_TX_BFD, OUTPUT);
  digitalWrite(SIM_TX_BFD, HIGH);
  pinMode(SIM_RX_BFD, INPUT_PULLUP);
  simCom.begin(BAUDRATE);
  xDelay(1000);
  return negoSimComBaudRate(BAUDRATE);
}

bool negoSimComBaudRate(int32_t baud) {
  Serial.println(F("\n== nego baud rate =="));
  simCom.begin(baud);
  if (sendATcmd(F("AT"), "OK", 1000, 16) != 0) {
    Serial.println(F("\nSim7000 current baud rate is 9600 :)"));
    return sendATcmd(F("ATE0"), "OK", 1000, 4);
  }
  return false;
}

void serialEnd() {
  while (simCom.available()) {
    simCom.read();
  }
  simCom.flush();
  xDelay(300);
  simCom.end();
  // pull down RX TX to save power
  pinMode(SIM_TX_BFD, OUTPUT);
  pinMode(SIM_RX_BFD, OUTPUT);
  digitalWrite(SIM_TX_BFD, LOW);
  digitalWrite(SIM_RX_BFD, LOW);
}

bool isSimComOn() { return digitalRead(SIM_STATUS_BFD); }

bool powerOnSimCom() {
  Serial.println(F("\nPowerOnSimCom"));
  delay(50);
  digitalWrite(SIM_PWRKEY, LOW);
  xDelay(1050);
  digitalWrite(SIM_PWRKEY, HIGH);
  xDelay(4000);
  for (int8_t i = 0; i < 10; i++) {
    if (isSimComOn()) {
      return true;
    }
    xDelay(500);
  }
  return false;
}

bool tryPowerOnSimCom(int8_t tries) {
  Serial.println(F("\nTry to power on SimCom:"));
  if (isSimComOn()) {
    Serial.println(F("\nSimCom is already on"));
    Serial.println(F("succeed."));
    return true;
  }

  for (int8_t i = 0; i < 3; i++) {
    if (powerOnSimCom()) {
      Serial.println(F("succeed."));
      return true;
    }
  }

  return false;
}

bool tryPowerOffSimCom() {
  Serial.println(F("\nTry to power off SimCom:"));
  if (isSimComOn() == false) {
    return true;
  }

  for (int8_t i = 0; i < 3; i++) {
    if (hardPowerOffSimCom()) {
      Serial.println(F("Power off Sim succeed."));
      return true;
    }
  }
  return false;
}

bool hardPowerOffSimCom() {
  Serial.println(F("\nhardPowerOffSimCom"));
  delay(50);
  digitalWrite(SIM_PWRKEY, LOW);
  xDelay(1300);
  digitalWrite(SIM_PWRKEY, HIGH);
  int8_t i = 0;
  for (; i < 20; i++) {
    if (isSimComOn() == false) {
      return true;
    }
    xDelay(500);
  }
  return false;
}

bool hardResetSimCom() {
  Serial.println(F("Reset SIM7000"));
  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, LOW);
  xDelay(2000);
  digitalWrite(SIM_RESET, HIGH);
  xDelay(4000);
  for (int8_t i = 0; i < 10; i++) {
    if (isSimComOn()) {
      return true;
    }
    xDelay(500);
  }
  return false;
}

void openbearer() {
  // set CSTT - if it is already set, then no need to do again...
  sendATcmd(F("AT+CSTT?"), "OK", 1000);
  if (strstr(response, APN) != NULL) {
    // this means the cstt has been set, so no need to set again!
    Serial.println(F("CSTT already set to APN ...no need to set again"));
  } else {
    sendATcmd(F("AT+CSTT=\"" APN "\""), "OK", 1000);
  }
  // close open bearer
  sendATcmd(F("AT+SAPBR=2,1"), "OK", 1000);
  if (strstr(response, "1,1") == NULL) {
    if (strstr(response, "1,3") == NULL) {
      sendATcmd(F("AT+SAPBR=0,1"), "OK", 1000);
    }
    sendATcmd(F("AT+SAPBR=3,1,\"APN\",\"" APN "\""), "OK", 1000);  // set bearer apn
    sendATcmd(F("AT+SAPBR=1,1"), "OK", 1000);
  }
}

void closeBearer() { sendATcmd(F("AT+SAPBR=0,1"), "OK", 1000); }

bool isValidDateTime(const DATETIME &dt) {
  int8_t dtyr = dt.yr % 100;
  if (dt.mt < 0 || dt.mt > 12) {
    return false;
  }
  if (dt.dy < 0 || dt.dy > 31) {
    return false;
  }
  if (dt.hr < 0 || dt.hr > 24) {
    return false;
  }
  if (dt.mn < 0 || dt.mn > 60) {
    return false;
  }
  if (dt.sc < 0 || dt.sc > 60) {
    return false;
  }
  return true;
}

void datetime2y2k(const DATETIME* cal, const int16_t tz4, uint32_t* y2ksecs){
    struct tm t;
    t.tm_sec = cal->sc; 
    t.tm_min = cal->mn; 
    t.tm_hour = cal->hr; 
    t.tm_mday = cal->dy; 
    t.tm_mon = (cal->mt -1); 
    t.tm_year = cal->yr + 100; 
    t.tm_isdst = 0;

    *y2ksecs = mktime(&t);
    *y2ksecs -= 900*(uint32_t)tz4;//we need to subtract the timezone given in quarter hour increments (900 seconds)

}

void y2k2datetime(DATETIME* cal, const uint32_t y2ksecs){
  uint32_t seconds, minutes, hours, days, year, month;
  uint32_t day_of_week;
  seconds = y2ksecs;

  minutes  = seconds / 60;
  hours    = minutes / 60;
  days     = hours   / 24;

  seconds = seconds % 60;
  minutes = minutes % 60;
  hours   = hours   % 24;

  /* avr time starts in 2000 */
  year    = 2000;

  while(1)
  {
    int     leap_year   = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint16_t days_in_year = leap_year ? 366 : 365;
    if (days >= days_in_year)
    {
      days      -= days_in_year;
      ++year;
    }
    else
    {
      /* calculate the month and day */
      static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(month = 0; month < 12; ++month)
      {
        uint8_t dim = days_in_month[month];

        /* add a day to feburary if this is a leap year */
        if (month == 1 && leap_year)
          ++dim;

        if (days >= dim)
          days -= dim;
        else
          break;
      }
      break;
    }
  }

  cal->sc  = seconds;
  cal->mn  = minutes;
  cal->hr = hours;
  cal->dy = days + 1;
  cal->mt  = month + 1;
  cal->yr = year - 2000;//for correct calender epoch
}

bool pullNetTime() {
  int ret = 0;

  if (sendATcmd(F("AT+CLTS=1"), "OK", 1000) == false) {
    return false;
  }
  if (sendATcmd(F("AT+CCLK?"), "OK", 1000) == false) {
    return false;
  }
  char *p = strstr(response, "+CCLK: \"");
  if (p == NULL) {
    return false;
  }

  int16_t tz4;
  ret = sscanf(p, "%s \"%d/%d/%d,%d:%d:%d%d", response, &g_datetime.yr,
         &g_datetime.mt, &g_datetime.dy, &g_datetime.hr, &g_datetime.mn,
         &g_datetime.sc, &tz4);
  if(ret == 8){//number of matches)
    /* fix timezone offset to UTC */
    uint32_t y2ksec;
    datetime2y2k(&g_datetime, tz4, &y2ksec);
    y2k2datetime(&g_datetime, y2ksec);
    return true;
  }
  return false;
}

int _test_pullNetTime() {
  int ret = 0;
  ret = tryPowerOnSimCom(POWER_ON_TRY_TIMES);
  if(ret == false){return -1;}

  ret = serialBegin();
  if(ret == false){return -2;}

  ret = netReg();
  if(ret == false){return -3;}

  while(1){
    pullNetTime();
    _dbgPrintDateTime(g_datetime);
    delay(5000);
  }
}

void _dbgPrintDateTime(const DATETIME& datetime) {
  Serial.print(datetime.yr);
  Serial.print("-");
  Serial.print(datetime.mt);
  Serial.print("-");
  Serial.print(datetime.dy);
  Serial.print(" ");
  Serial.print(datetime.hr);
  Serial.print(":");
  Serial.print(datetime.mn);
  Serial.print(":");
  Serial.print(datetime.sc);
  Serial.println();
}


void initRTC() {
    while (!MCP7940.begin()) {
        Serial.println(F("Unable to find RTC(MCP7940). Checking again in 1000 ms."));
        xDelay(1000);
    }
    Serial.println(F("RTC(MCP7940) initialized."));
    Serial.println(F("Starting RTC..."));
    while (!MCP7940.deviceStatus()){ // Turn oscillator on if necessary
        bool deviceStatus =  MCP7940.deviceStart(); // Start oscillator and return state
        if (!deviceStatus){       // If it didn't start
            xDelay(1000);
        }
    }
    Serial.println(F("RTC started."));

    MCP7940.setBattery(true);     // enable battery backup mode

    Serial.print(F("RTC DateTime:"));
    DateTime now = MCP7940.now();
    char RTCBuffer[30];
    memset(RTCBuffer, '\0', sizeof(RTCBuffer));
    sprintf(RTCBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
            now.day(), now.hour(), now.minute(), now.second());
    Serial.println(RTCBuffer);
}

void updateRTC(const DATETIME *dt) {
  if (dt == NULL) {
    MCP7940.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    DateTime datetime = DateTime(dt->yr, dt->mt, dt->dy, dt->hr, dt->mn, dt->sc);
    MCP7940.adjust(datetime);
  }
  Serial.print(F("RTC Date/Time update to:"));
  DateTime now = MCP7940.now();
  char RTCBuffer[30];
  memset(RTCBuffer, '\0', sizeof(RTCBuffer));
  sprintf(RTCBuffer, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
          now.day(), now.hour(), now.minute(), now.second());
  Serial.println(RTCBuffer);
}

void afterWakeup() {
  Serial.println(F("\n*** afterWakeup() ***"));
  Serial.flush();
  // detachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT));
  detachInterrupt(RTC_INTERRUPT);
  noSleep();
}

// void SleepySeconds(uint32_t secs) {
//   // set RTC
//   DateTime now = MCP7940.now();
//   now = now + TimeSpan(0, 0, 0, secs);
//   MCP7940.setAlarm(0, matchAll, now, true);
//   //attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT), afterWakeup, LOW);
//   attachInterrupt(RTC_INTERRUPT, afterWakeup, LOW);

//   sleepMode(SLEEP_POWER_DOWN);
//   // sleepMode(SLEEP_STANDBY);
//   sleep();
// }

// MDS-09: old sensor reset routine removed; MDS stays powered.
bool isSDAcessable() {
  return SD.begin(SD_CS);
}

// mainly write data form titles
bool SDSetup() {
  Serial.println(F("Initializing SD card..."));
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  if (!isSDAcessable()) {
    Serial.println(F("Initialization failed!"));
    return false;
  }
  Serial.println(F("Initialization done."));


  DateTime now = MCP7940.now(); // get the current time
  memset(response, '\0', sizeof(response));
  sprintf(response, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
          now.day(), now.hour(), now.minute(), now.second());

  if(SD_LOG_ALL_DATA){
    myFile = SD.open(SD_LOG_FILE_ALL_DATA, FILE_WRITE);
    // if the file opened okay, write to it:
    if (myFile) {
      Serial.println(F("SD card has been set up successfully..."));
      myFile.print("Arduino has been reset. New data starts at:");
      myFile.println(response);
      myFile.println(F("Time,Index,Velocity_m_s,Depth_m,VelocityValid,DepthValid,VelocityCount,DepthCount,SignalQuality"));
      // close the file:
      myFile.close();
      Serial.println(F("SD card and log file set up is Done."));
    } else {
      // if the file didn't open, print an error:
      Serial.print(F("Error opening "));
      Serial.println(SD_LOG_FILE_ALL_DATA);
      return false;
    }
  }

  myFile = SD.open(SD_LOG_FILE_WEB_DATA, FILE_WRITE);
  // if the file opened okay, write to it:
  if (myFile) {
    Serial.println(F("SD card has been set up successfully..."));
    myFile.print("Arduino has been reset. New data starts at:");
    myFile.println(response);
    myFile.println(F("Time,Index,Velocity_m_s,Depth_m,VelocityValid,DepthValid,VelocityCount,DepthCount,SignalQuality"));
    // close the file
    myFile.close();
    Serial.println(F("SD card and log file set up is Done."));
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("Error opening "));
    Serial.println(SD_LOG_FILE_WEB_DATA);
    return false;
  }

  return true;
}

void logToSD(const char* fileName) {
  unsigned long index = 0;
  if (strstr(fileName, SD_LOG_FILE_ALL_DATA)) {
    sdRecordIndex++;
    index = sdRecordIndex;
  } else if (strstr(fileName, SD_LOG_FILE_WEB_DATA)) {
    webRecordIndex++;
    index = webRecordIndex;
  }

  if (isSDAcessable() == false) {
    return;
  }

  myFile = SD.open(fileName, FILE_WRITE);

  if (myFile) {
    DateTime now = MCP7940.now(); // get the current time
    memset(response, '\0', sizeof(response));
    sprintf(response, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
            now.day(), now.hour(), now.minute(), now.second());
    myFile.print(response);
    myFile.print(F(","));
    myFile.print(index);
    myFile.print(F(","));
    // MDS-10: blank invalid values; raw counts are 0/1, averaged counts are per field.
    if (velocityValid) myFile.print(velocityVal, 4);
    myFile.print(',');
    if (depthValid) myFile.print(depthVal, 4);
    myFile.print(',');
    myFile.print(velocityValid ? 1 : 0);
    myFile.print(',');
    myFile.print(depthValid ? 1 : 0);
    myFile.print(',');
    myFile.print(velocityValidCount);
    myFile.print(',');
    myFile.print(depthValidCount);
    myFile.print(',');
    myFile.println(CSQ); // one complete CSV row, including signal quality
    myFile.close();
    Serial.println(F("Just Saved To SD Card."));

  } else {
    // if the file didn't open, print an error:
    Serial.println(F("error opening datafile"));
    xDelay(1000);
  }
}

bool getLastStatusTimeFromSD() {
  char strtime[32];
  bool ret = false;
  if (isSDAcessable() == false) {
    return false;
  }

  File myFile = SD.open(SD_LOG_FILE_ALL_DATA, FILE_READ);

  if (!myFile) {
    return false;
  }

  unsigned long fileSize = myFile.size();

  if (fileSize < 10) {
    myFile.close();
    return false;
  }

  // seek to the "end" of the file
  if (fileSize > 200) {
    myFile.seek(fileSize - 200);
  }

  // seek to the start of a line
  while (myFile.available()) {
    if (myFile.read() == '\n') {
      break;
    }
  }

  memset(strtime, '\0', sizeof(strtime));
  uint16_t i = 0;
  char c = 0;
  bool commaFound = false;
  while (myFile.available()) {
    c = myFile.read();
    if (c == ',' && commaFound == false) {
      sscanf(strtime, "%d-%d-%d %d:%d:%d", &g_datetime.yr, &g_datetime.mt,
             &g_datetime.dy, &g_datetime.hr, &g_datetime.mn, &g_datetime.sc);
      commaFound = true;
      Serial.println(strtime);
      continue;
    } else if (c == '\n') {
      memset(strtime, '\0', sizeof(strtime));
      i = 0;
      commaFound = false;
      continue;
    } else if (i < sizeof(strtime) && commaFound == false) {
      strtime[i] = c;
      i++;
    }
  }

  myFile.close();

  char buf[20];
  sprintf(buf, "%d-%d-%d %d:%d:%d", g_datetime.yr, g_datetime.mt, g_datetime.dy,
          g_datetime.hr, g_datetime.mn, g_datetime.sc);
  Serial.print(F("Last status time from SD:"));
  Serial.println(buf);

  return isValidDateTime(g_datetime);
}

bool logToWeb() {
  bool ret = false;
  static uint16_t upload_fails = 0;
  static uint16_t power_on_fails = 0;
  static uint16_t net_reg_fails = 0;

  Serial.print(F("upload fails: "));
  Serial.print(upload_fails);
  Serial.print(F(" power_on_fails: "));
  Serial.print(power_on_fails);
  Serial.print(F(" net_reg_fails: "));
  Serial.println(net_reg_fails);

  if (net_reg_fails > NET_REG_FAILS_LIMIT_BEGIN &&
      (net_reg_fails % NET_REG_FAILS_LIMIT_RATE)) {
    Serial.println(F("netReg failure high. Limiting logging"));
    net_reg_fails++;
    return false;
  }

  if (net_reg_fails > NET_REG_FAILS_LIMIT_BEGIN &&
      !(net_reg_fails % 3 * NET_REG_FAILS_LIMIT_RATE)) {
    Serial.println(F("netReg failure high. Reseting"));
    hardResetSimCom();
  }

  Serial.println(F("Initialising SIM 7000"));
  simCom.begin(BAUDRATE);

  uint8_t reset = false;
  for (uint8_t i = 0; i < TRY_POWER_ON_MAX_RESETS; i++) {
    if (reset == true) {
      reset = false;
      power_on_fails++;
      if (power_on_fails < POWER_ON_FAILS_RESET_LIMIT_BEGIN ||
          !(power_on_fails % POWER_ON_FAILS_RESET_LIMIT_RATE)) {
        Serial.println(F("cannot communicate with simCom. resetting"));
        hardResetSimCom();
      }
    }

    ret = tryPowerOnSimCom(POWER_ON_TRY_TIMES);
    if(ret == false){reset = true; continue;}

    ret = serialBegin();
    if(ret == false){reset = true; continue;}

    break;
  }
  if(ret == false){goto cleanup;}

  ret = netReg();
  if (ret == false) {
    net_reg_fails++;
    goto net_reg_cleanup;
  } else {
    net_reg_fails = 0;
  }

  openbearer();
  CBCread();
  CSQread();

  if (pullNetTime()) {
    Serial.print("Updating to UTC time: ");
    _dbgPrintDateTime(g_datetime);
    updateRTC(&g_datetime);
  } else {
    Serial.println(F("Network time unavailable; keeping existing RTC time."));
  }

  for (uint8_t i = 0; i < HTTP_UPLOAD_TRY_TIMES; i++) {
    ret = Transmit();
    if (ret == false) {
      upload_fails++;
      if (upload_fails > HTTP_FAILS_RESET_THRESHOLD) {
        upload_fails = 0;
        Serial.println(F("HTTP failure rate high. resetting"));
        hardResetSimCom();
        goto net_reg_cleanup;
      }
      continue;
    }
    break;
  }

  closeBearer();

net_reg_cleanup:
  netUnreg();

cleanup:
  serialEnd();
  tryPowerOffSimCom();
  Serial.print(F("LogToWeb() ret: "));
  Serial.println(ret);
  return ret;
}

// SCAN-01: readable results shared by setup and loop; no raw Modbus dumps.
void printSetupReading(const __FlashStringHelper* label, float value, bool valid,
                       MdsP2510::Status status, uint8_t exceptionCode) {
  Serial.print(label);
  if (valid) Serial.print(value, 6);
  else Serial.print(F("INVALID"));
  Serial.print(F(" valid="));
  Serial.print(valid ? 1 : 0);
  Serial.print(F(" status="));
  if (!flowSample.ready) {
    Serial.print(F("NotReady"));
  } else if (status == MdsP2510::Status::Ok && !valid) {
    Serial.print(F("NonfiniteValue"));
  } else {
    switch (status) {
      case MdsP2510::Status::Ok: Serial.print(F("OK")); break;
      case MdsP2510::Status::InvalidArgument: Serial.print(F("InvalidArgument")); break;
      case MdsP2510::Status::Timeout: Serial.print(F("Timeout")); break;
      case MdsP2510::Status::ShortFrame: Serial.print(F("ShortFrame")); break;
      case MdsP2510::Status::WrongAddress: Serial.print(F("WrongAddress")); break;
      case MdsP2510::Status::WrongFunction: Serial.print(F("WrongFunction")); break;
      case MdsP2510::Status::WrongByteCount: Serial.print(F("WrongByteCount")); break;
      case MdsP2510::Status::CrcError: Serial.print(F("CrcError")); break;
      case MdsP2510::Status::ModbusException: Serial.print(F("ModbusException")); break;
      default: Serial.print(F("Unknown")); break;
    }
  }
  if (status == MdsP2510::Status::ModbusException) {
    Serial.print(F(" exception=0x"));
    Serial.print(exceptionCode, HEX);
  }
  Serial.println();
}

void setup() {
  BoSLFlow::end(); // BOOT-01: rails off until the first setup measurement
  // set up pins for sim7000
  pinMode(SIM_BUF_EN, OUTPUT);
  digitalWrite(SIM_BUF_EN, HIGH);

  pinMode(SIM_PWRKEY, OUTPUT);
  digitalWrite(SIM_PWRKEY, HIGH);

  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, HIGH);

  pinMode(SIM_STATUS_BFD, INPUT);
  xDelay(1000); // we need to wait a little bit for the pin inputs to settle

  pinMode(RTC_INTERRUPT, INPUT_PULLUP);


  // BOOT-01: initialize RTC before taking the first measurement in setup.
  Serial.begin(BAUDRATE);

  g_datetime.init(0, 0, 0, 0, 0, 0);

  initRTC();

  if (getLastStatusTimeFromSD()) { // g_datetime will be updated if successful
     updateRTC(&g_datetime);
  } else {
     Serial.println(F("No valid time from SD card; keeping existing RTC time."));
  }

  // BOOT-02: first power/read/off cycle happens here, not on entry to loop().
  const unsigned long firstCycleStartedMs = millis();
  readFlowSensor(); // switches both rails off before any modem activity
  // SCAN-02: print the actual first sample before time sync or HTTP.
  Serial.println(F("=== SETUP FIRST SENSOR SCAN (power off) ==="));
  printSetupReading(F("Velocity_m_s="), flowSample.velocityMps,
                    flowSample.velocityValid, flowSample.velocityStatus,
                    flowSample.velocityException);
  printSetupReading(F("Depth_m="), flowSample.waterLevelM,
                    flowSample.waterLevelValid, flowSample.waterLevelStatus,
                    flowSample.waterLevelException);
  Serial.print(F("LOG_TO_WEB="));
  Serial.println(LOG_TO_WEB ? F("true") : F("false"));
  if (!velocityValid && !depthValid) {
    Serial.println(F("Both readings invalid: HTTP data upload will be skipped."));
  }
  Serial.flush();

  // WEB-02: startup web/time-sync attempt depends only on LOG_TO_WEB.
  // Time-sync and HTTP success differ; advance baseline only on HTTP success.
  if (LOG_TO_WEB && logToWeb()) {
    noLoggingLoopCounter = 0;
    preVelocityAverage = velocityVal;
    preDepthAverage = depthVal;
    preVelocityValid = velocityValid;
    preDepthValid = depthValid;
    haveUploadBaseline = true;
  }
  // SD logging is independent of the web-enable setting.
  // Timestamps are logging time, not acquisition time before network work.
  isSDSetupDone = SDSetup();
  if (isSDSetupDone) {
    if (SD_LOG_ALL_DATA) logToSD(SD_LOG_FILE_ALL_DATA);
    logToSD(SD_LOG_FILE_WEB_DATA);
  }

  Serial.println(F("Setup Done!"));
  // BOOT-04: avoid another measurement immediately on entry to loop().
  // If setup's network work overruns six minutes, the next cycle starts now.
  finishMeasurementCycle(firstCycleStartedMs);
}

// MDS-12: trigger on absolute OR relative change, as the original config describes.
bool readingChanged(double value, double previous, double absoluteThreshold) {
  const double difference = fabs(value - previous);
  if (difference > absoluteThreshold) return true;
  // Near zero, use the absolute threshold; avoid divide-by-zero/noise triggers.
  return fabs(previous) > absoluteThreshold &&
         difference > fabs(previous) * THRESHOLD_PERCENT / 100.0;
}

int shouldLogToWeb(void) {
  if (!velocityValid && !depthValid) return false;
  if (CONSTANT_LOGGING || !haveUploadBaseline) return true;
  if (velocityValid != preVelocityValid || depthValid != preDepthValid) return true;
  if (velocityValid && readingChanged(velocityVal, preVelocityAverage,
                                      VELOCITY_DIFF_THRESHOLD_MPS)) return true;
  if (depthValid && readingChanged(depthVal, preDepthAverage,
                                   DEPTH_DIFF_THRESHOLD_M)) return true;
  return noLoggingLoopCounter >=
         (MAX_HTTP_INTERVAL_MINUTES + LOOP_INTERVAL_MINUTES - 1) / LOOP_INTERVAL_MINUTES;
}

void loop() {

  unsigned long millisTemp = millis(); // MDS-24: atomic read on the 8-bit MCU
  Serial.println(F("\n*** loop begin ***"));

  // WEB-03: no debug-pin bypass; always follow normal measurement/SD scheduling.

  if (noResetLoopsCount <= MIN_NON_RESET_LOOP_COUNT) {
    noResetLoopsCount++;
  }

  // PWR-05: power on, settle, read both values, power off (including on errors).
  readFlowSensor();
  // SCAN-03: show every raw scan, before averaging, SD logging or upload decisions.
  Serial.println(F("=== SENSOR SCAN (power off) ==="));
  printSetupReading(F("Velocity_m_s="), flowSample.velocityMps,
                    flowSample.velocityValid, flowSample.velocityStatus,
                    flowSample.velocityException);
  printSetupReading(F("Depth_m="), flowSample.waterLevelM,
                    flowSample.waterLevelValid, flowSample.waterLevelStatus,
                    flowSample.waterLevelException);

  // log every scan result to SD card
  if (isSDAcessable() && SD_LOG_ALL_DATA) {
    // if SD setup is not done in setup(), try again here
    if (isSDSetupDone == false) {
      isSDSetupDone = SDSetup(); // mainly write data form titles
    }
    if (isSDSetupDone) {
      logToSD(SD_LOG_FILE_ALL_DATA);
    }
  }

  // MDS-15: per-field counts exclude timeouts, CRC failures and nonfinite values.
  if (noLoggingLoopCounter < 0xFFFFFFFDUL) noLoggingLoopCounter++;
  scanCounterForHttp++;
  if (velocityValid) { velocitySum += velocityVal; velocityAverageCount++; }
  if (depthValid) { depthSum += depthVal; depthAverageCount++; }

  if (scanCounterForHttp >= LOOPS_COUNT_PER_AVERAGE) {
    getAverage();
    if (!isSDSetupDone) isSDSetupDone = SDSetup();
    // Keep every averaging window on SD, including all-invalid windows.
    if (isSDSetupDone) logToSD(SD_LOG_FILE_WEB_DATA);

    if (shouldLogToWeb()) {
      // MDS-16: failed HTTP attempts must not advance the comparison baseline.
      if (!LOG_TO_WEB || logToWeb()) {
        noLoggingLoopCounter = 0;
        preVelocityAverage = velocityVal;
        preDepthAverage = depthVal;
        preVelocityValid = velocityValid;
        preDepthValid = depthValid;
        haveUploadBaseline = true;
      }
    }
    scanCounterForHttp = 0;
    velocitySum = depthSum = 0;
    velocityAverageCount = depthAverageCount = 0;
  }

  finishMeasurementCycle(millisTemp);
}

// PWR-04: start-to-start interval includes warmup, reads and logger work.
void finishMeasurementCycle(unsigned long millisTemp) {
  // sleep
  unsigned long millisDuration =
      millis() - millisTemp; // unsigned subtraction handles millis rollover
  if (millisDuration < LOOP_INTERVAL_MILLIS) {
    millisTemp = LOOP_INTERVAL_MILLIS - millisDuration;
    unsigned long secs = (unsigned long)(millisTemp / 1000);
    Serial.println(F("\n*** Sleep ***"));
    xDelay(millisTemp % 1000);
    deepSleepSecs(secs);
  } else {
    Serial.println(F("\n*** No sleep, next loop ***"));
  }
}

// MDS-17: replace both original sensor protocols with validated Modbus readings.
void readFlowSensor() {
  BoSLFlow::begin(SENSOR_STARTUP_MS); // PWR-05: conservative warmup on every cycle
  while (!BoSLFlow::ready()) delay(10);
  flowSample = BoSLFlow::read();
  BoSLFlow::end(); // PWR-05: shut down immediately, before logging or uploading
  velocityValid = flowSample.velocityValid;
  depthValid = flowSample.waterLevelValid;
  velocityVal = velocityValid ? flowSample.velocityMps : NAN;
  depthVal = depthValid ? flowSample.waterLevelM : NAN;
  velocityValidCount = velocityValid ? 1 : 0;
  depthValidCount = depthValid ? 1 : 0;
}

void getAverage() {
  velocityValidCount = velocityAverageCount;
  depthValidCount = depthAverageCount;
  velocityValid = velocityValidCount > 0;
  depthValid = depthValidCount > 0;
  velocityVal = velocityValid ? velocitySum / velocityValidCount : NAN;
  depthVal = depthValid ? depthSum / depthValidCount : NAN;
}

// 48 bytes cover even a finite AVR float's largest fixed-point representation.
String flowNumber(double value, uint8_t decimals) {
  char buffer[48];
  dtostrf(value, 1, decimals, buffer);
  return String(buffer);
}

// reads battery voltage from response char array
void storeCBCresponse() {
    memset(CBC, '\0', sizeof(CBC));
    char* resp_data = strstr(response, "CBC:");
    if(resp_data == NULL){return;}

    Serial.println(resp_data);
    sscanf(resp_data, "CBC: %*d,%*d,%4s",CBC);
    Serial.println(CBC);
}


// like delay but lower power and more dodgy//
void xDelay(uint32_t tmz) {
  if (Serial) {
    Serial.flush();
  }
  uint32_t tmzslc = tmz / 64;
  // 64 chosen as ballance between power savings and time spent in full clock mode
  clock_prescale_set(clock_div_64);
  delay(tmzslc);
  clock_prescale_set(clock_div_1);

  cli();
  timer0_millis += 63 * tmzslc;
  sei();

  delay(tmz - 64 * tmzslc);
}

// MDS-18: unused legacy test harness removed; modem operation stays intact.

////power down cellular functionality////
void netUnreg() { sendATcmd(F("AT+CFUN=0"), "OK", 1000); }

//AT+COPS=? (to find all the networks) can take upwards of 20 minutes!!!
//AT+COPS=? this is the same (about 20 minutes) if search NB-IoT
//AT+COPS=? when only searching for LTE M1, it takes 90s

/*
Typical use:

-> AT+COPS=?

<- +COPS: (2,"Telstra Mobile","Telstra","50501",7),(1,"vodafone AU","voda AU","50503",9),(1,"Optus AU","Optus","50502",9),(1,"Telstra Mobile","Telstra","50501",9),,(0,1,2,3,4),(0,1,2)

<- OK

Response format:
(stat,oper long,oper short, mccmnc, format,netact)
stat: 0, unknown; 1, available; 2, current; 3, forbidden
oper: operator name string
mccmnc: operator mccmnc
netact: 0, GSM; 1 GSM compact; 3, GSM EGPRS; 7, LTE-M1; 9, LTE-NB (NB-IoT)
*/

int _dbgPrintOperators(void) {
  Serial.print(F("Operators Length: "));
  Serial.println(operators_len);

  for (int i = 0; i < operators_len; ++i) {
    Serial.print(F("Operator "));
    Serial.print(i + 1);
    Serial.print(F(": Status - "));
    Serial.print(operators[i].status);
    Serial.print(F(", MCCMNC - "));
    Serial.print(operators[i].mccmnc);
    Serial.print(F(", Netact - "));
    Serial.print(operators[i].netact);
    Serial.print(F(", RSSI - "));
    Serial.println(operators[i].rssi);
  }
  return true;
}

int storeOperators(){
  char* start = NULL;
  char* end = NULL;
  size_t len = 0;

  operators_len = 0;

  start = strstr(response, "+COPS: ");//make sure we are reading the +COPS line
  if(start == NULL){return false;}

  for(uint8_t i = 0; i < MAX_OPERATORS; i++){
    if(start == strstr(start, ",,")){return true;} //we are at the end of the list

    start = strstr(start, "(");//start of a operators listing
    if(start == NULL){return false;}
    // MDS-21: check the pointer before arithmetic; parse into correctly sized ints.
    end = strstr(start, "),");
    if(end == NULL){return false;}
    end++;

    len = end - start;//check that the matches are of reasonable length
    if(len > CHARBUFF){return false;}

    Operator* o = &operators[operators_len];
    int status = 0, netact = 0;
    uint8_t matches = sscanf(start, "(%d,%*[^,],%*[^,],\"%6[^\"]\",%d)", &status, o->mccmnc, &netact);
    if(matches == 3 && status >= 0 && status <= 3 && netact >= 0 && netact <= 255){
      o->status = static_cast<uint8_t>(status);
      o->netact = static_cast<uint8_t>(netact);
      o->rssi = 99;//not known or not detectable
      operators_len++;
        }
else {return false;}

    start = end;
  }
  return false;
}

int compare_rssi(const void *a, const void *b){
  int16_t rssi_a = ((Operator*)a)->rssi;
  int16_t rssi_b = ((Operator*)b)->rssi;

  if(rssi_a == 99 && rssi_b == 99){return 0;}
  if(rssi_a == 99){return 1;}
  if(rssi_b == 99){return -1;}

  return  rssi_a > rssi_b ? -1 : 
           (rssi_b > rssi_a ?  1 : 0);

}

int netSearch(void){
  int ret = false;

  ret = sendATcmd(F("AT+COPS=?"), "OK", 1800000, 1);//I've observed this take as long as 20 minutes before so lets make the timeout 30 minutes
  if(!ret){return false;}

  storeOperators();
  _dbgPrintOperators();

  for(uint8_t i = 0; i < operators_len; i++){
    Operator* o = &operators[i];
    ret = netSelect(o->mccmnc);
    if(!ret){continue;}

    char mccmnc_current[7] = "\0\0\0\0\0\0\0";
    getMccmnc(mccmnc_current, sizeof(mccmnc_current));
    if(strcmp(o->mccmnc,mccmnc_current)){
      Serial.println(F("ERR: operators do not match"));
      continue;
    }

    getRSSI(&o->rssi);

  }

  qsort(operators, operators_len, sizeof(Operator), compare_rssi);

  return true;
}


int isRegistered(const uint32_t timeout_ms = 591){
  int ret = 0;
  const uint32_t retry_delay_ms = 591;//empirically measured
  const uint32_t attempts = timeout_ms/retry_delay_ms;

  for(uint32_t i = 0; i < attempts; i++){
    ret = sendATcmd(F("AT+CREG?"), "OK", 1000, 1);
    if(ret){
      int registered = strstr(response, "+CREG: 0,5")
		       || (ACCEPT_NON_ROAMING && strstr(response, "+CREG: 0,1"));
      if(registered){
        Serial.println(F("CREG: registered"));
        return true;
      }
    }
    Serial.print(F("Waiting for registration, "));
    Serial.print(i);Serial.print(F("/"));Serial.println(attempts);
    xDelay(50);
  }
  Serial.println(F("CREG: not registered"));
  return false;
}

//this supports needs to have knowledge of netact too
int netSelect(const char* mccmnc){
  int ret = 0;
  const uint32_t cops_timeout_ms = 150000;//120s specified in the AT command manual
  const uint32_t creg_timeout_ms = 150000;//this one is more how long we are willing to wait;

  if(mccmnc == NULL){
    Serial.println(F("Automatic network registration attempt"));
    ret = sendATcmd(F("AT+COPS=0"), "OK", cops_timeout_ms, 1);
    if(ret == 0){return false;}
  }else{
    Serial.print(F("Attempting to register to: "));
    Serial.println(mccmnc);
    char atcmd[64];
    snprintf(atcmd, sizeof(atcmd), "AT+COPS=4,2,%s", mccmnc);
    ret = sendATcmd(atcmd, "OK", cops_timeout_ms, 1);
    if(ret == 0){return false;}
  }

  ret = isRegistered(creg_timeout_ms);

  return ret;
}

////register to network////
bool netReg(void) {
  int ret;
  static char mccmnc[7] = "\0\0\0\0\0\0\0";
  static uint8_t network_searched = 0;

  if(mccmnc[0] == '\0'){//if uninitialised
    strncpy(mccmnc, MCCMNC, sizeof(mccmnc) -1 );
    mccmnc[sizeof(mccmnc) - 1] = '\0';
  }

  if (sendATcmd(F("AT+CFUN=1"), "OK", 10000, 2) == false) { // enable the modem
    return false;
  }

  if(ALLOW_GSM){
    sendATcmd(F("AT+CNMP=2"), "OK",
              500, 1); //enable GSM
  }else{
    sendATcmd(F("AT+CNMP=38"), "OK",
              500, 1); //disable GSM (its 2024!)
  }

  if(ALLOW_NBIOT){
    sendATcmd(F("AT+CMNB=3"), "OK",
               500, 1); //enable NB-IoT
  }else{
    sendATcmd(F("AT+CMNB=1"), "OK",
               500, 1); //disable NB-IoT (simbase does not offer NB-IoT)
  }

  sendATcmd(F("AT+CGDCONT=1,\"IP\",\"" APN "\""), "OK",
            2000); // set IPv4 and apn

  ret = isRegistered();
  if(ret){return ret;}

  ret = netSelect(mccmnc);
  if(ret){
    ret = getMccmnc(mccmnc, sizeof(mccmnc));
    if(ret){
      Serial.print(F("Updating network preference to: "));
      Serial.println(mccmnc);
    }
    return true;
  }

  ret = netSelect(MCCMNC);
  if(ret){
    ret = getMccmnc(mccmnc, sizeof(mccmnc));
    if(ret){
      Serial.print(F("Updating network preference to: "));
      Serial.println(mccmnc);
    }
    return true;
  }

  ret = netSelect(NULL);
  if(ret){
    ret = getMccmnc(mccmnc, sizeof(mccmnc));
    if(ret){
      Serial.print(F("Updating network preference to: "));
      Serial.println(mccmnc);
    }
    return true;
  }

  if(!network_searched){
    Serial.println(F("Searching networks, this could take > 30 minutes."));
    netSearch();
    network_searched = true;//not we never do this again regardless of if the search was successfull
  }

  //attempt to connect to networks in order of signal strength
  for(uint8_t i = 0; i < operators_len; i++){
    Operator* o = &operators[i];
    if(o->rssi == 99){continue;}
    ret = netSelect(o->mccmnc);
    if(ret){
      ret = getMccmnc(mccmnc, sizeof(mccmnc));
      if(ret){
        Serial.print(F("Updating network preference to: "));
        Serial.println(mccmnc);
      }
      return true;
    }
  }

  Serial.println(F("Unable to connect to network"));
  return false;
}


int getRSSI(uint8_t* rssi_p){
  // MDS-22: %d requires int*, not uint8_t*; default unknown on every failure.
  if (rssi_p == NULL) return false;
  *rssi_p = 99;
  int parsedRssi = 99;
  int ret = 0;
  sendATcmd(F("AT+CSQ"), "OK", 1000, 1);
  char* start = strchr(response, '+');
  if(start == NULL){return false;}

  ret = sscanf(start, "+CSQ: %d,%*d", &parsedRssi);
  if(ret == 1 && ((parsedRssi >= 0 && parsedRssi <= 31) || parsedRssi == 99)){
    *rssi_p = static_cast<uint8_t>(parsedRssi);
    return true;
  }
  return false;
}


int getMccmnc(char* mccmnc, size_t mccmnc_size){
  int ret = 0;
  sendATcmd(F("AT+CPSI?"), "OK", 1000, 1);
  char mcc[4] = "\0\0\0\0";
  char mnc[4] = "\0\0\0\0";
  char* start = strchr(response, '+');
  if(start == NULL){return false;}

  ret = sscanf(start, "+CPSI: %*[^,],%*[^,],%3[^-]-%3[^,],", mcc, mnc);
  if(ret == 2){
    snprintf(mccmnc, mccmnc_size, "%s%s", mcc,mnc);
    return true;
  }
  return false;
}

////sends at command, checks for reply////
bool sendATcmd(String ATcommand, char *expctAns, uint32_t timeout,
               int8_t tries) {
  uint32_t timeStart;
  bool answer;
  uint8_t a = 0;

  do {
    a++;
    Serial.println();
    Serial.println(ATcommand);

    answer = false;
    timeStart = 0;
    xDelay(100);

    while (simCom.available() > 0) {
      simCom.read(); // Clean the input buffer
    }

    simCom.println(ATcommand); // Send the AT command

    uint8_t i = 0;
    timeStart = millis();
    memset(response, '\0', CHARBUFF); // Initialize the string

    // this loop waits for the answer
    do {
      if (simCom.available() != 0) {
        // MDS-23: never overwrite RAM or lose the string terminator on long replies.
        if (i >= CHARBUFF - 1) {
          answer = false;
          break;
        }
        response[i] = simCom.read();
        i++;
        // check if the desired answer is in the response of the module
        if (strstr(response, expctAns) != NULL) {
          answer = true;
        }
      }
      // Waits for the asnwer with time out
    } while ((answer == false) && ((millis() - timeStart) < timeout));

    Serial.print(F("response:"));
    Serial.println(response);

    if (expctAns == "\0") {
      answer = true;
    }

  } while (answer == false && a < tries);

  a = 0;
  return answer;
}

void CBCread() {
  // get GNSS data
  if (sendATcmd(F("AT+CBC"), "OK", 1000)) {

    storeCBCresponse();
  }
}

void CSQread() {
  uint8_t csq;
  getRSSI(&csq);
  memset(CSQ, '\0', sizeof(CSQ));
  sprintf(CSQ, "%" PRIu8, csq);
}

////TRANSMITS LAST GPS CORDINATES TO WEB////
bool Transmit() {
  if (!velocityValid && !depthValid) return false; // MDS-19: no fabricated data

  bool ret = false;

  dataStr = "AT+HTTPPARA=\"URL\",\"http://www.bosl.com.au/IoT/";
  dataStr += SITE_DIR;
  dataStr += "/scripts/WriteMe_v2.php?SiteName=";
  dataStr += SITE_ID;
  dataStr += ".csv";
  // MDS-19: keep all legacy query keys; -99.99 means missing, never a reading.
  dataStr += "&Depth=";
  dataStr += depthValid ? flowNumber(depthVal, 3) : String("-99.99");
  dataStr += "&Velocity=";
  dataStr += velocityValid ? flowNumber(velocityVal, 4) : String("-99.99");
  dataStr += "&Batt=";
  dataStr += CBC;
  dataStr += "&CSQ=";
  dataStr += CSQ;
  dataStr += "\"";

  sendATcmd(F("AT+HTTPINIT"), "OK", 1000);
  if (strstr(response, "ERROR")) {
    return false;
  }
  sendATcmd(F("AT+HTTPPARA=\"CID\",1"), "OK", 1000);
  if (strstr(response, "ERROR")) {
    return false;
  }

  sendATcmd(dataStr, "OK", 2000);  // check what the HTTPTIMEOUT is

  if (sendATcmd(F("AT+HTTPACTION=0"), "OK", 5000) == false) {
    return false;
  }

  ret = sendATcmd(F("AT+HTTPSTATUS?"), "+HTTPACTION: 0,200", 1000, 1);
  if (strstr(response, "ERROR")) {     
    return false; // if the response is ERROR, no need to continue
  }

  // await the "200" response a bit longer to ensure http uploading is done
  if (ret == false) {
    ret = sendATcmd(F("AT+HTTPSTATUS?"), "+HTTPACTION: 0,200", 1000, 15); // TODO: what is the best timeout?
  }

  sendATcmd(F("AT+HTTPSTATUS?"), "+HTTPSTATUS: GET,0,0,0", 1000, 2);
  sendATcmd(F("AT+HTTPTERM"), "OK", 1000, 3);
  return ret; // MDS-20: report failure unless an HTTP 200 was actually observed
}

void deepSleepSecs(int32_t sec) {
  // turn adc off
  ADCSRA &= ~(1 << ADEN);

  while (sec >= 4) {
    sec -= 4;
    // wdt wakeup timer
    wdt_enable(WDTO_4S);
    WDTCSR |= (1 << WDIE);

    // sleep
    sleepMode(SLEEP_POWER_DOWN);
    sleep();
    noSleep();
    timer0_millis += 4000;
  }

  while (sec >= 1) {
    sec -= 1;
    // wdt wakeup timer
    wdt_enable(WDTO_1S);
    WDTCSR |= (1 << WDIE);

    // sleep
    sleepMode(SLEEP_POWER_DOWN);
    sleep();
    noSleep();
    timer0_millis += 1000;
  }

  // adc on
  ADCSRA |= (1 << ADEN);
}

ISR(WDT_vect) {
  // WDIE & WDIF is cleared in hardware upon entering this ISR
  wdt_disable();
}
