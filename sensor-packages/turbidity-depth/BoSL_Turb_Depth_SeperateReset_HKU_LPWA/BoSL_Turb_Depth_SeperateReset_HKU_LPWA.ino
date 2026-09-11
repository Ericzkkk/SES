// BoSL turbidity-depth v1.0.1: CPSI parser fix (logic edited 2026-09-09).
// Documentation published 2026-09-11. See release notes for validation scope.
// Modem powers off after each upload attempt.
// BoSL production LPWA revision: user-selected LTE-M or NB-IoT.
// See CHANGES.md for the complete before/after change list and test limitations.

/*************************************************
/************ Connections for setup **************
* Turbidity VPP (Red wire)     -- SWC3V3
* Turbidity GND (Black wire)   -- GND
* Turbidity TX (Yellow wire)   -- TX3  (RX3 FOR NEW TURB)
* Turbidity RX (Green wire)    -- RX3  (TX3 FOR NEW TURB)
* Turbidity RESET (White wire) -- D14
*
* Depth VPP (Red wire)         -- SWB3V3
* Depth GND (Black wire)       -- GND
* Depth TX (Yellow wire)       -- TX2                                       
* Depth RX (White wire)        -- RX2
* Depth RESET (Blue wire)      -- D13
*
* Debug Pin       -- A15
* Connect Debug Pin to GND will enable debug mode
* In debug mode, only printing turbidity readings,
* network register and http uploading will be skipped
*****************************************************/

/* this code requires arduino IDE > 2.3.2  */

// ********* Configurations for setup ************
#define SITE_DIR "testing"
#define SITE_ID "Eric_Testing"
// Only these three CELLULAR settings need editing. Existing site/sensor settings remain below.
// Non-empty MCCMNC: stay on that operator. Empty "": automatic operator selection.
// Example for the M2M One Telstra SIM tested in Australia:
#define APN "nbiot"
#define MCCMNC "45412"
#ifndef CELLULAR_MODE
#define CELLULAR_MODE 2 // 1 = LTE-M only; 2 = NB-IoT only. No automatic fallback.
#endif
#if CELLULAR_MODE != 1 && CELLULAR_MODE != 2
#error "CELLULAR_MODE must be 1 (LTE-M) or 2 (NB-IoT)"
#endif
#define SD_LOG_ALL_DATA      true       // save raw sensor readings to SD card.
#define SD_LOG_FILE_ALL_DATA "log.txt"  // file to save raw sensor readings (max 8.3 characters)
#define SD_LOG_FILE_WEB_DATA  "web.txt" // file to save averaged sensor readings (max 8.3 characters)
#define LOG_TO_WEB true                 // logging to web only occurs when true. if false SD log times may not be accurate
#define LOOP_INTERVAL_MINUTES 1         // sensor scan interval (minutes)
#define AVERAGING_INTERVAL_MINUTES 6    // averaging and upload interval (minutes)
#define CONSTANT_LOGGING true           // if true, it will log to web every
                                        // AVERAGING_INTERVAL_MINUTES

// * Variable logging control * // only used when CONSTANT_LOGGING is false
#define MAX_HTTP_INTERVAL_MINUTES 60 // maximum upload interval (minutes)
/*  In variable logging, a log is triggered when any of the sensor readings change by 
 *  more than an absolute or relative threshold from the previous upload.
 *  The below define these absolute and relative thresholds.
 */
#define THRESHOLD_PERCENT 10.0    // relative threshold for sensor readings (%)
#define PRESS_DIFF_THRESHOLD 15.0 // absolute threshold for depth readings (mm)
#define EC_DIFF_THRESHOLD 10.0     // absolute threshold for EC readings (mS/cm)
#define TEMP_DIFF_THRESHOLD 1.0   // absolute threshold for temperature readings (deg C)
#define TURB_DIFF_THRESHOLD 10.0   // absolute threshold for temperature readings (NTU)
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

#include <MCP7940.h>
#include <SD.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

// *** special macro ***
#define IMPOSSIBLE_SENSOR_VALUE -99.99
#define POWER_ON_TRY_TIMES 5
#define HTTP_UPLOAD_TRY_TIMES 3 // to fix the issues of simcom or bad signal.
// first time try power off simcom, second time try reset simcom, third time
// and forward only try power off simcom to avoid frequent reset

// *** Pins ***
// For debugging
#define DEBUG_PIN A15 // Connect to GND for debugging(only print sensor
                      // reading, no network needed), also saving power
// For the Depth sensor
#define Depth_VPP EN_SWB // 66
#define Depth_RESET 13 // D13/PH5
#define DepEC_RX RX2
#define DepEC_TX TX2
// SD card
// For the turbidity sensor
#define TURB_VPP EN_SWC // 65
#define TURB_RESET 14 // D14/PH6
#define TURB_RX RX3
#define TURB_TX TX3

// *** Serials ***
#define simCom Serial1
#define DepECScan Serial2
#define TurbScan Serial3

// *** Constants ***
#define LOOP_INTERVAL_MILLIS (LOOP_INTERVAL_MINUTES * 60000) // sensor scan
                                                             // interval(ms)
#define BAUDRATE 9600 // talk to SIM7000 baud rate
#define CHARBUFF 254  // Bounded response snapshot; overflow causes failure, never overwrite
#define SIM_RESET_MIN_INTERVAL_HOURS 3
#define MIN_NON_RESET_LOOP_COUNT (SIM_RESET_MIN_INTERVAL_HOURS * 60 / LOOP_INTERVAL_MINUTES)
#define LOOPS_COUNT_PER_AVERAGE (AVERAGING_INTERVAL_MINUTES / LOOP_INTERVAL_MINUTES)

// *** global variables ***
// atmega timer0_millis
extern volatile unsigned long timer0_millis;
// RTC
MCP7940_Class MCP7940;
struct DATETIME {
public:
  init(int16_t yr, int16_t mt, int16_t dy, int16_t hr, int16_t mn, int16_t sc) {
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
// sensor variables
String DEPTH_EC[3];  // 1-depth reading, 2-Temp reading, 3-EC reading
String TRUBIDITY[2]; // 1-turb with LED, 2-turb without LED
double pressVal, tempVal, ECVal, turbidityVal, turbiditywoledVal;
char Temp[10];
char Press[10];
char EC[10];
char turbidity[10];
char turbiditywoled[10];

// SD card variables
File myFile;
unsigned long sdRecordIndex = 0;
unsigned long webRecordIndex = 0;
bool isSDSetupDone = false;
// SIM RESET control
uint32_t noResetLoopsCount = MIN_NON_RESET_LOOP_COUNT;

// *** logging control ***
double pressSum = 0;
double ECSum = 0;
double tempSum = 0;
double turbiditySum = 0;
double turbiditywoledSum = 0;
double prePressAverage = 0;
double preECAverage = 0;
double preTempAverage = 0;
double preTurbidityAverage = 0;
double preTurbiditywoledAverage = 0;
uint8_t scanCounterForHttp = 0;
uint32_t noLoggingLoopCounter = 0;

// *** function prototypes ***
bool negoSimComBaudRate(int32_t baud = BAUDRATE);
bool tryPowerOnSimCom(int8_t tries = POWER_ON_TRY_TIMES);
bool sendATcmd(String ATcommand, const char *expctAns, uint32_t timeout = 5000,
               int8_t tries = 1);

// Cellular runtime: one explicitly selected technology; no mode-preference storage.
static bool modemNeedsReset = false;
static bool httpActionPending = false, httpActionSeen = false;
static int httpStatusCode = -1;
static uint32_t httpResponseBytes = 0;
static bool atWaiting = false, atDone = false, atOK = false;
static bool responseOverflow = false;
static size_t responseUsed = 0, modemLineUsed = 0;
static char modemLine[192];
static bool modemLineOverflow = false;
static const uint32_t COPS_TIMEOUT_MS = 180000UL;
static const uint32_t LTE_REG_TIMEOUT_MS = 180000UL;
static const uint32_t NB_REG_TIMEOUT_MS = 300000UL;
static const uint32_t HTTP_TIMEOUT_MS = 120000UL;

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
  clearModemTransactionState();
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
  clearModemTransactionState();
  for (int8_t i = 0; i < 10; i++) {
    if (isSimComOn()) {
      return true;
    }
    xDelay(500);
  }
  return false;
}


bool bearerIsOpen() {
  if (!sendATcmd(F("AT+SAPBR=2,1"), "OK", 5000, 1)) return false;
  int cid=-1, state=-1;
  char ip[40] = "";
  const char *p = strstr(response, "+SAPBR:");
  return p && sscanf(p, "+SAPBR: %d,%d,\"%39[^\"]\"", &cid, &state, ip) == 3 &&
         cid == 1 && state == 1 && ip[0] && strcmp(ip,"0.0.0.0") != 0;
}

bool openbearer() {
  if (!sendATcmd(F("AT+CGATT?"), "OK", 5000, 1) ||
      !strstr(response, "+CGATT: 1")) return false;
  if (!sendATcmd(F("AT+SAPBR=2,1"),"OK",5000,1)) return false;
  int cid=-1, state=-1;
  const char *p=strstr(response,"+SAPBR:");
  if (!p || sscanf(p,"+SAPBR: %d,%d",&cid,&state)!=2 || cid!=1) return false;
  // Reconfigure even an existing bearer: it may belong to the previous SIM/APN.
  if (state!=3 && !sendATcmd(F("AT+SAPBR=0,1"),"OK",65000UL,1)) return false;
  if (!sendATcmd(F("AT+SAPBR=3,1,\"APN\",\"" APN "\""), "OK", 5000, 1) ||
      !sendATcmd(F("AT+SAPBR=1,1"), "OK", 85000UL, 1)) return false;
  return bearerIsOpen();
}

void closeBearer() {
  if (!modemNeedsReset && !httpActionPending)
    sendATcmd(F("AT+SAPBR=0,1"), "OK", 65000UL, 1);
}

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

  if (sendATcmd(F("AT+CLTS=1"), "OK", 5000) == false) {
    return false;
  }
  if (sendATcmd(F("AT+CCLK?"), "OK", 5000) == false) {
    return false;
  }
  char *p = strstr(response, "+CCLK: \"");
  if (p == NULL) {
    return false;
  }

  int16_t tz4;
  ret = sscanf(p, "+CCLK: \"%d/%d/%d,%d:%d:%d%d", &g_datetime.yr,
         &g_datetime.mt, &g_datetime.dy, &g_datetime.hr, &g_datetime.mn,
         &g_datetime.sc, &tz4);
  if(ret == 7){//number of matches)
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
    MCP7940.adjust();
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

// RESET sensors
void resetPin(int8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(10);
  digitalWrite(pin, HIGH);
  pinMode(pin, INPUT);
}

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
      myFile.println(F("Time,Index,EC,Temp,Press,Turbidity,Turbiditywoled,Signal Quality"));
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
    myFile.println(F("Time,Index,EC,Temp,Press,Turbidity,Turbiditywoled,Signal Quality"));
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
    myFile.print(ECVal);
    myFile.print(F(","));
    myFile.print(tempVal);
    myFile.print(F(","));
    myFile.print(pressVal);
    myFile.print(F(","));
    myFile.print(turbidityVal);
    myFile.print(F(","));
    myFile.println(turbiditywoledVal);
    myFile.print(F(","));
    myFile.print(CSQ);
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
      !(net_reg_fails % (3UL * NET_REG_FAILS_LIMIT_RATE))) {
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

  if (!openbearer()) {
    ret = false;
    upload_fails++;
    goto net_reg_cleanup;
  }
  CBCread();
  CSQread();

  if (pullNetTime()) {
    Serial.print("Updating to UTC time: ");
    _dbgPrintDateTime(g_datetime);
    updateRTC(&g_datetime);
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
      if (modemNeedsReset || httpActionPending) break;
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

void setup() {
  // _test();
  // while(1);


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

  pinMode(DEBUG_PIN, INPUT_PULLUP);

  pinMode(TURB_VPP, OUTPUT);
  digitalWrite(TURB_VPP, HIGH);

  pinMode(Depth_VPP, OUTPUT);
  digitalWrite(Depth_VPP, HIGH);

  // begin serial
  Serial.begin(BAUDRATE);

  // read sensors
  resetPin(TURB_RESET);
  turbread();
  digitalWrite(TURB_VPP, LOW);
  resetPin(Depth_RESET);
  DepECread();
  digitalWrite(Depth_VPP, LOW);

  // save the first reading for next comparison
  prePressAverage = pressVal;
  preECAverage = ECVal;
  preTempAverage = tempVal;
  preTurbidityAverage = turbidityVal;
  preTurbiditywoledAverage = turbiditywoledVal;

  turbConvertChar();
  depthConvertChar();

  g_datetime.init(0, 0, 0, 0, 0, 0);

  initRTC();
  // updateRTC(&g_datetime); // set RTC to compile time

  // if (getLastStatusTimeFromSD()) { // g_datetime will be updated if successful
  //   updateRTC(&g_datetime);
  // } else {
  //   Serial.println(F("No valid time from SD card, RTC is set to compile time."));
  // }

  logToWeb(); // g_datetime and RTC will be updated if this process is successful


  isSDSetupDone = SDSetup();
  if (isSDSetupDone) {
    if(SD_LOG_ALL_DATA){logToSD(SD_LOG_FILE_ALL_DATA);}
    logToSD(SD_LOG_FILE_WEB_DATA);
  }

  Serial.println(F("Setup Done!"));
}

double max(double a, double b){
  return a > b ? a : b;
}

double min(double a, double b){
  return a < b ? a : b;
}

/*
int isClose(double a, double b, double rtol, double atol){
  double tol = max(rtol * max(abs(a),abs(b)), atol);
  return abs(a-b) <= tol;
}
*/

int isClose(double a, double b, double rtol, double atol){
  double tol = min(rtol * min(abs(a),abs(b)), atol);
  return abs(a-b) > tol;
}

int _test_isClose(){

  if(isClose(1,2, 0.2, 0.1) != false){return -1;}

  if(isClose(1,0.8, 0.2, 0.1) != true){return -2;}

  if(isClose(1,1.1,0.02,0.2) != true){return -3;}

  if(isClose(0,2, 0.2, 0.1) != false){return -4;}

  if(isClose(0,0.1, 0.05, 0.2) != true){return -5;}

  if(isClose(0.2,0, 0.2, 0.0) != false){return -6;}

  if(isClose(4.1,4.0, 0.2, 0.0) != true){return -7;}

  if(isClose(10.0,14.0, 0.3, 1.0) != true){return -8;}

  return 0;
}


int shouldLogToWeb(void){
  if (CONSTANT_LOGGING) {
    Serial.println(F("Constant logging, log now."));
    return true;
  }

  if(!isClose(pressVal, prePressAverage, THRESHOLD_PERCENT/100.0, PRESS_DIFF_THRESHOLD)){
    Serial.println(F("Large change in pressure, log now."));
    return true;
  }

  if(!isClose(ECVal, preECAverage, THRESHOLD_PERCENT/100.0, EC_DIFF_THRESHOLD)){
    Serial.println(F("Large change in EC, log now."));
    return true;
  }

  if(!isClose(tempVal, preTempAverage, THRESHOLD_PERCENT/100.0, TEMP_DIFF_THRESHOLD)){
    Serial.println(F("Large change in temperature, log now."));
    return true;
  }

  if(!isClose(turbidityVal - turbiditywoledVal, preTurbidityAverage - preTurbiditywoledAverage, THRESHOLD_PERCENT/100.0, TURB_DIFF_THRESHOLD)){
    Serial.println(F("Large change in turbidity, log now."));
    return true;
  }

  // if no logging for a long time, then log
  if (noLoggingLoopCounter * LOOP_INTERVAL_MINUTES >=
      MAX_HTTP_INTERVAL_MINUTES) {
    Serial.println(F("Long time since last log, log now."));
    return true;
  }

  return false;
}

int _test_shouldLogToWeb(){

  pressVal = 0.0;
  prePressAverage = 0.0;
  ECVal = 1.0;
  preECAverage = 1.2;
  tempVal = 0.0;
  preTempAverage = 0.0;
  turbidityVal = 0.0;
  turbiditywoledVal = 0.0;
  preTurbidityAverage = 0.0;
  preTurbiditywoledAverage = 0.0;
  noLoggingLoopCounter = 0;
  if(shouldLogToWeb() != false){return -1;}

  pressVal = 0.0;
  prePressAverage = 0.0;
  ECVal = 1.0;
  preECAverage = 1.2;
  tempVal = 10.0;
  preTempAverage = 15.0;
  turbidityVal = 0.0;
  turbiditywoledVal = 0.0;
  preTurbidityAverage = 0.0;
  preTurbiditywoledAverage = 0.0;
  noLoggingLoopCounter = 0;
  if(shouldLogToWeb() != true){return -2;}

  pressVal = 100.0;
  prePressAverage = 115.0;
  ECVal = 1.0;
  preECAverage = 1.2;
  tempVal = 10.0;
  preTempAverage = 11.0;
  turbidityVal = 0.0;
  turbiditywoledVal = 0.0;
  preTurbidityAverage = 0.0;
  preTurbiditywoledAverage = 0.0;
  noLoggingLoopCounter = 0;
  if(shouldLogToWeb() != false){return -3;}

  return 0;
}

void loop() {

  unsigned long millisTemp = timer0_millis;
  Serial.println(F("\n*** loop begin ***"));

  // if debug mode, only print sensor readings
  // no network register and http uploading
  // mainly for debugging
  // connect DEBUG_PIN to GND to enable debug mode
  // set up a calibration mode
  if (digitalRead(DEBUG_PIN) == 0) {
    digitalWrite(TURB_VPP, HIGH);

    turbread();
    digitalWrite(TURB_VPP, LOW);
    return;
  }

  bool logToWebDone = false;

  if (noResetLoopsCount <= MIN_NON_RESET_LOOP_COUNT) {
    noResetLoopsCount++;
  }

  // read turbidity sensor
  digitalWrite(TURB_VPP, HIGH);
  xDelay(100);
  resetPin(TURB_RESET);
  turbread();
  digitalWrite(TURB_VPP, LOW);

  // read depth sensor
  digitalWrite(Depth_VPP, HIGH);
  xDelay(100);
  resetPin(Depth_RESET);
  DepECread();
  digitalWrite(Depth_VPP, LOW);

  // log every scan result to SD card
  if (isSDAcessable() && SD_LOG_ALL_DATA) {
    // if SD setup is not done in setup(), try again here
    if (isSDSetupDone == false) {
      isSDSetupDone = SDSetup(); // mainly write data form titles
    }
    if (isSDSetupDone) {
      turbConvertChar();
      depthConvertChar();
      logToSD(SD_LOG_FILE_ALL_DATA);
    }
  }

  // count scan times and add up the valid sensor readings
  scanCounterForHttp++;
  pressSum += pressVal;
  ECSum += ECVal;
  tempSum += tempVal;
  turbiditySum += turbidityVal;
  turbiditywoledSum += turbiditywoledVal;

  // calculate average and decide if to log to web
  if (scanCounterForHttp >= LOOPS_COUNT_PER_AVERAGE) {
    // calculate average
    getAverage();

    if (shouldLogToWeb()) {
      // convert average values to char string
      turbConvertChar();
      depthConvertChar();
      // if SD setup is not done, try again here
      if (isSDSetupDone == false) {
        isSDSetupDone = SDSetup(); // mainly write data form titles
      }
      // backup the data to SD card
      if (isSDSetupDone) {
        logToSD(SD_LOG_FILE_WEB_DATA);
      }
      // log to web server
      if (LOG_TO_WEB ? logToWeb() : true) {
        noLoggingLoopCounter = 0;
        logToWebDone = true;
      }

      // reset the control variables
      // TODO: if log to web failed, should we reset the control variables?
      prePressAverage = pressVal;
      preECAverage = ECVal;
      preTempAverage = tempVal;
      preTurbidityAverage = turbidityVal;
      preTurbiditywoledAverage = turbiditywoledVal;
    }

    // clear sums and counter no matter if log to web or not
    scanCounterForHttp = 0;
    pressSum = 0;
    ECSum = 0;
    tempSum = 0;
    turbiditySum = 0;
    turbiditywoledSum = 0;
  }

  if (logToWebDone == false) {
    if (noLoggingLoopCounter < 0xFFFFFFFD) { // avoid overflow
      noLoggingLoopCounter++;
    }
  }

  // sleep
  unsigned long millisDuration =
      timer0_millis - millisTemp; // TODO: 50days overflow to 0
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

void turbread() {
  Serial.println(F("Scanning tubidity sensor..."));
  char turbraw[10];
  char turbwoledraw[10];
  memset(turbraw, '\0', 10);
  memset(turbwoledraw, '\0', 10);
  uint32_t tout = 0;
  uint8_t bytin = 0;
  // needs to turn the power on here via npn transistor

  TurbScan.begin(9600);

  // check if the sensor has correctly been reseted
  tout = millis();
  while (millis() - tout < 10000) {
    if (TurbScan.available() != 0) {
      bytin = TurbScan.read();
      if (bytin == 'R') {
        break;
      }
    }
  }

  TurbScan.write('V');

  TurbScan.setTimeout(1500);
  if (TurbScan.readBytes(turbraw, 10) > 0) {
    turbidityVal = atof(turbraw);
  } else {
    turbidityVal = IMPOSSIBLE_SENSOR_VALUE;
  }

  TurbScan.write('T');
  TurbScan.setTimeout(1500);
  if (TurbScan.readBytes(turbwoledraw, 10) > 0) {
    turbiditywoledVal = atof(turbwoledraw);
  } else {
    turbiditywoledVal = IMPOSSIBLE_SENSOR_VALUE;
  }

  TurbScan.write('S');

  TurbScan.flush();
  TurbScan.end();


  Serial.print(F("Turbidity with LED: "));
  Serial.println(turbidityVal);
  Serial.print(F("Turbidity without LED: "));
  Serial.println(turbiditywoledVal);
}

void turbConvertChar() {
  memset(turbidity, '\0', sizeof(turbidity));
  dtostrf(turbidityVal, 2, 2, turbidity);
  memset(turbiditywoled, '\0', sizeof(turbiditywoled));
  dtostrf(turbiditywoledVal, 2, 2, turbiditywoled);
}

void DepECread() {
  Serial.println(F("Scanning depth sensor..."));
  uint32_t tout = 0;
  uint8_t bytin = 0;
  String input = "";
  char data[20];
  memset(data, '\0', sizeof(data));
  // Keep track of current position in array
  int counter = 0;
  // Keep track of the last comma so we know where to start the substring
  int lastIndex = 0;
  // needs to turn the power on here via npn transistor

  DepECScan.begin(9600);

  tout = millis();

  while (millis() - tout < 10000) {
    if (DepECScan.available() != 0) {
      bytin = DepECScan.read();
      if (bytin == 'R') {
        break;
      }
    }
    DepECScan.write('A');
    delay(100);
  }

  DepECScan.write('V');

  DepECScan.setTimeout(1500);
  int8_t nBytes = DepECScan.readBytes(data, 20);
  input = data;

  for (int i = 0; i < input.length(); i++) {
    if (input.substring(i, i + 1) == ",") {
      // Grab the piece from the last index up to the current position
      // and store it
      DEPTH_EC[counter] = input.substring(lastIndex, i);
      // Update the last position and add 1, so it starts from the next
      // character
      lastIndex = i + 1;
      // Increase the position in the array that we store into
      counter++;
    }

    // If we're at the end of the string (no more commas to stop us)
    if (i == input.length() - 1) {
      // Grab the last part of the string from the lastIndex to the end
      DEPTH_EC[counter] = input.substring(lastIndex, i);
    }
  }

  if (nBytes > 0) {
    pressVal = DEPTH_EC[0].toFloat();
    tempVal = DEPTH_EC[1].toFloat();
    ECVal = DEPTH_EC[2].toFloat();
  } else {
    pressVal = IMPOSSIBLE_SENSOR_VALUE;
    tempVal = IMPOSSIBLE_SENSOR_VALUE;
    ECVal = IMPOSSIBLE_SENSOR_VALUE;
  }

  input = "";
  counter = 0;
  lastIndex = 0;

  DepECScan.print('S');

  DepECScan.flush();
  DepECScan.end();

  Serial.print(F("Temp: "));
  Serial.println(tempVal);
  Serial.print(F("Depth: "));
  Serial.println(pressVal);
  Serial.print(F("EC: "));
  Serial.println(ECVal);
}

void depthConvertChar() {
  memset(Temp, '\0', sizeof(Temp));
  dtostrf(tempVal, 2, 2, Temp);
  memset(Press, '\0', sizeof(Press));
  dtostrf(pressVal, 2, 2, Press);
  memset(EC, '\0', sizeof(EC));
  dtostrf(ECVal, 2, 2, EC);
}

void getAverage() {
  Serial.print(F("repeat count: "));
  Serial.println(scanCounterForHttp);
  Serial.print(F("ECSum: "));
  Serial.println(ECSum);
  Serial.print(F("TempSum: "));
  Serial.println(tempSum);
  Serial.print(F("PressSum: "));
  Serial.println(pressSum);
  Serial.print(F("TurbiditySum: "));
  Serial.println(turbiditySum);
  Serial.print(F("TurbiditywoledSum: "));
  Serial.println(turbiditywoledSum);

  ECVal = (double)ECSum / (double)scanCounterForHttp;
  tempVal = (double)tempSum / (double)scanCounterForHttp;
  pressVal = (double)pressSum / (double)scanCounterForHttp;
  turbidityVal = (double)turbiditySum / (double)scanCounterForHttp;
  turbiditywoledVal = (double)turbiditywoledSum / (double)scanCounterForHttp;

  ECSum = tempSum = pressSum = turbiditySum = turbiditywoledSum = 0;
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

void _test(void){
  int ret;

  Serial.begin(BAUDRATE);

  
  // ret = _test_isClose();
  // if(ret == 0){Serial.println(F("_test_isClose(): pass"));}
  //   else        {Serial.print(F("_test_isClose(): ")); Serial.print(ret); Serial.println(F(", fail!"));}

  // ret = _test_shouldLogToWeb();
  // if(ret == 0){Serial.println(F("_test_shouldLogToWeb(): pass"));}
  //   else        {Serial.print(F("_test_shouldLogToWeb(): ")); Serial.print(ret); Serial.println(F(", fail!"));}

  pinMode(SIM_BUF_EN, OUTPUT);
  digitalWrite(SIM_BUF_EN, HIGH);

  pinMode(RTC_INTERRUPT, INPUT_PULLUP);

  pinMode(DEBUG_PIN, INPUT_PULLUP);

  pinMode(SIM_PWRKEY, OUTPUT);
  digitalWrite(SIM_PWRKEY, HIGH);

  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, HIGH);

  pinMode(SIM_STATUS_BFD, INPUT);

  // begin serial
  simCom.begin(BAUDRATE);

  Serial.println(F("Initialising SIM 7000"));

  xDelay(1000);

  tryPowerOnSimCom(POWER_ON_TRY_TIMES);

  serialBegin();

  netReg();

  ret = _test_pullNetTime();
  if(ret == 0){Serial.println(F("_test_pullNetTime(): pass"));}
  else        {Serial.print(F("_test_pullNetTime(): ")); Serial.print(ret); Serial.println(F(", fail!"));}


}



////power down cellular functionality////
void netUnreg() {
  if (!modemNeedsReset && !httpActionPending)
    sendATcmd(F("AT+CFUN=0"), "OK", 20000UL, 1);
}

bool cellularSettingsValid() {
  const char *apn=APN, *plmn=MCCMNC;
  const size_t n=strlen(plmn);
  if (!apn[0] || strlen(apn)>63 || (n && n!=5 && n!=6)) return false;
  for (const char *p=apn; *p; ++p)
    if (!(isalnum((unsigned char)*p) || *p=='.' || *p=='-')) return false;
  for (const char *p=plmn; *p; ++p) if (*p<'0' || *p>'9') return false;
  return true;
}

bool verifyReply(String command, const char *expected) {
  if (!sendATcmd(command,"OK",5000,1)) return false;
  if (strstr(response,expected)) return true;
  Serial.println(F("CONFIGURATION READBACK MISMATCH."));
  return false;
}

bool waitForSIM() {
  const uint32_t start=millis();
  do {
    if (sendATcmd(F("AT+CPIN?"),"OK",5000,1) && strstr(response,"+CPIN: READY"))
      return true;
    if (modemNeedsReset) return false;
    modemDelay(1000);
  } while ((uint32_t)(millis()-start)<30000UL);
  Serial.println(F("SIM not READY: check insertion/PIN/activation."));
  return false;
}

int isRegistered(const uint32_t timeout_ms = 5000UL) {
  const uint32_t start=millis();
  do {
    if (!sendATcmd(F("AT+CEREG?"),"OK",5000,1)) return false;
    const char *p=strstr(response,"+CEREG:");
    int mode=-1, state=-1;
    if (p && sscanf(p,"+CEREG: %d,%d",&mode,&state)==2) {
      if (state==1 || state==5) return true;
      if (state==3) {
        Serial.println(F("EPS registration denied; CEER does not establish subscription cause alone."));
        sendATcmd(F("AT+CEER"),"OK",5000,1);
        return false;
      }
    }
    if ((uint32_t)(millis()-start)>=timeout_ms) break;
    modemDelay(2000);
  } while ((uint32_t)(millis()-start)<timeout_ms);
  Serial.println(F("EPS registration timed out; coverage remains unknown."));
  return false;
}

bool configureRat(uint8_t rat) {
  // Remove previously saved diagnostic single-band masks on BOTH technologies.
  if (!sendATcmd(F("AT+CFUN=0"),"OK",20000UL,1) ||
      !sendATcmd(F("AT+CNMP=38"),"OK",5000,1) ||
      !sendATcmd(rat==1 ? F("AT+CMNB=1") : F("AT+CMNB=2"),"OK",5000,1) ||
      !sendATcmd(F("AT+CBANDCFG=\"CAT-M\",1,2,3,4,5,8,12,13,18,19,20,26,28,39"),"OK",5000,1) ||
      !sendATcmd(F("AT+CBANDCFG=\"NB-IOT\",1,2,3,5,8,12,13,17,18,19,20,26,28"),"OK",5000,1) ||
      !sendATcmd(F("AT+CGDCONT=1,\"IP\",\"" APN "\""),"OK",5000,1)) return false;
  if (!verifyReply(F("AT+CNMP?"),"+CNMP: 38") ||
      !verifyReply(F("AT+CMNB?"),rat==1 ? "+CMNB: 1" : "+CMNB: 2") ||
      !sendATcmd(F("AT+CBANDCFG?"),"OK",5000,1)) return false;
  if (!strstr(response,"\"CAT-M\",1,2,3,4,5,8,12,13,18,19,20,26,28,39") ||
      !strstr(response,"\"NB-IOT\",1,2,3,5,8,12,13,17,18,19,20,26,28")) return false;
  if (!verifyReply(F("AT+CGDCONT?"),"1,\"IP\",\"" APN "\"") ||
      !sendATcmd(F("AT+CFUN=1"),"OK",20000UL,1) || !waitForSIM()) return false;
  // B08/B10 reject some reporting commands with RF off: set after CFUN=1.
  return sendATcmd(F("AT+CEREG=0"),"OK",5000,1);
}

bool netReg(void) {
  if (!cellularSettingsValid()) {
    Serial.println(F("Invalid APN/MCCMNC. MCCMNC must be 5/6 digits or empty."));
    return false;
  }
  if (modemNeedsReset && (!hardResetSimCom() || !serialBegin())) return false;
  if (!sendATcmd(F("AT+CMEE=2"),"OK",5000,1) ||
      !sendATcmd(F("AT+CFUN=1"),"OK",20000UL,1) || !waitForSIM()) return false;
  // Fail explicitly if an earlier diagnostic left a cell lock.
  if (!verifyReply(F("AT+MCELLLOCK?"),"+MCELLLOCK: 0") ||
      !verifyReply(F("AT+NCELLLOCK?"),"+NCELLLOCK: 0")) return false;

  const uint8_t rat=CELLULAR_MODE;
  do { // One attempt on the selected RAT; failures go to the common report below.
    Serial.println(rat==1 ? F("Trying LTE-M") : F("Trying NB-IoT"));
    if (!sendATcmd(F("AT+COPS=2"),"OK",COPS_TIMEOUT_MS,1) || !configureRat(rat)) break;
    char select[48];
    if (strlen(MCCMNC))
      snprintf(select,sizeof(select),"AT+COPS=1,2,\"%s\",%u",MCCMNC,rat==1 ? 7 : 9);
    else strcpy(select,"AT+COPS=0");
    if (!sendATcmd(select,"OK",COPS_TIMEOUT_MS,1)) break;
    if (!isRegistered(rat==1 ? LTE_REG_TIMEOUT_MS : NB_REG_TIMEOUT_MS)) break;
    if (!sendATcmd(F("AT+CPSI?"),"OK",5000,1)) break;
    const bool ratMatches = strstr(response,rat==1 ? "LTE CAT-M1" : "LTE NB-IOT") != NULL;
    if (!ratMatches) { Serial.println(F("Serving RAT mismatch.")); break; }
    if (strlen(MCCMNC)) {
      char current[7]="";
      if (!getMccmnc(current,sizeof(current)) || strcmp(current,MCCMNC)) {
        Serial.println(F("Serving operator mismatch.")); break;
      }
    }
    const uint32_t attachStart=millis();
    do {
      if (!sendATcmd(F("AT+CGATT?"),"OK",5000,1)) break;
      if (strstr(response,"+CGATT: 1")) {
        Serial.println(F("EPS registered and packet attached."));
        sendATcmd(F("AT+CGNAPN"),"OK",5000,1);
        return !modemNeedsReset;
      }
      modemDelay(2000);
    } while ((uint32_t)(millis()-attachStart)<30000UL);
  } while (false);
  Serial.println(F("Selected LTE-M/NB-IoT mode did not connect; no automatic RAT fallback."));
  return false;
}


int getRSSI(uint8_t* rssi_p) {
  *rssi_p=99;
  if (!sendATcmd(F("AT+CSQ"),"OK",5000,1)) return false;
  const char *p=strstr(response,"+CSQ:");
  int value=-1;
  if (!p || sscanf(p,"+CSQ: %d",&value)!=1 || !((value>=0 && value<=31)||value==99)) return false;
  *rssi_p=(uint8_t)value;
  return true;
}

int getMccmnc(char* mccmnc, size_t mccmnc_size){
  int ret = 0;
  if (!mccmnc || mccmnc_size == 0) return false;
  mccmnc[0] = '\0';
  if (!sendATcmd(F("AT+CPSI?"), "OK", 5000, 1)) return false;
  char mcc[4] = "\0\0\0\0";
  char mnc[4] = "\0\0\0\0";
  // Unsolicited *PSUTTZ can contain "+40" before the solicited CPSI line.
  const char* start = strstr(response, "+CPSI:");
  if(start == NULL){return false;}

  char delimiter = '\0';
  ret = sscanf(start, "+CPSI: %*[^,],%*[^,],%3[0123456789]-%3[0123456789]%c", mcc, mnc, &delimiter);
  if(ret == 3 && delimiter == ',' && strlen(mcc) == 3 &&
     (strlen(mnc) == 2 || strlen(mnc) == 3) &&
     mccmnc_size > strlen(mcc) + strlen(mnc)){
    snprintf(mccmnc, mccmnc_size, "%s%s", mcc,mnc);
    return true;
  }
  return false;
}


void clearModemTransactionState() {
  modemNeedsReset = false;
  httpActionPending = httpActionSeen = false;
  atWaiting = atDone = atOK = false;
  modemLineUsed = 0;
  modemLineOverflow = false;
}

void processModemLine(const char *line) {
  int method = -1, code = -1;
  unsigned long bytes = 0;
  if (httpActionPending && sscanf(line, "+HTTPACTION: %d,%d,%lu", &method, &code, &bytes) == 3 &&
      method == 0 && code >= 100 && code <= 699) {
    httpActionSeen = true;
    httpActionPending = false;
    httpStatusCode = code;
    httpResponseBytes = bytes;
  }
  if (!atWaiting) return;
  const size_t count = strlen(line);
  if (count) {
    if (responseUsed + count + 2 < sizeof(response)) {
      memcpy(response + responseUsed, line, count);
      responseUsed += count;
      response[responseUsed++] = '\n';
      response[responseUsed] = '\0';
    } else responseOverflow = true;
  }
  if (strcmp(line, "OK") == 0) { atDone = true; atOK = true; }
  else if (strcmp(line, "ERROR") == 0 || strncmp(line, "+CME ERROR:", 11) == 0 ||
           strncmp(line, "+CMS ERROR:", 11) == 0) { atDone = true; atOK = false; }
}

void pollModem() {
  while (simCom.available()) {
    const char c = simCom.read();
    if (c == '\r') continue;
    if (c == '\n') {
      modemLine[modemLineUsed] = '\0';
      if (modemLineOverflow) {
        Serial.println(F("MODEM LINE TOO LONG: response not parsed."));
        if (atWaiting) responseOverflow = true;
      } else if (modemLineUsed) {
        Serial.println(modemLine);
        processModemLine(modemLine);
      }
      modemLineUsed = 0;
      modemLineOverflow = false;
    } else if (modemLineUsed < sizeof(modemLine)-1) {
      modemLine[modemLineUsed++] = c;
    } else modemLineOverflow = true;
  }
}

// Keep the modem UART clock running during network/HTTP waits.
void modemDelay(uint32_t duration) {
  const uint32_t start = millis();
  while ((uint32_t)(millis()-start) < duration) { pollModem(); delay(1); }
}

bool sendATcmd(String ATcommand, const char *expctAns, uint32_t timeout, int8_t tries) {
  const bool startupProbe = ATcommand == "AT" || ATcommand == "ATE0";
  if (modemNeedsReset) {
    Serial.println(F("AT BLOCKED: previous command unresolved; reset/power cycle required."));
    return false;
  }
  // Only UART negotiation may repeat on silence. Never overlap registration/HTTP commands.
  const uint8_t attempts = startupProbe && tries > 0 ? tries : 1;
  for (uint8_t attempt=0; attempt<attempts; ++attempt) {
    pollModem(); // Consume complete unsolicited lines instead of silently discarding them.
    response[0] = '\0'; responseUsed = 0; responseOverflow = false;
    atDone = atOK = false; atWaiting = true;
    Serial.print(F(">>> ")); Serial.println(ATcommand);
    simCom.println(ATcommand);
    const uint32_t started = millis();
    uint32_t progress = started;
    while (!atDone && (uint32_t)(millis()-started) < timeout) {
      pollModem();
      if ((uint32_t)(millis()-progress) >= 30000UL) {
        Serial.println(F("Waiting for modem command result..."));
        progress = millis();
      }
      delay(1);
    }
    atWaiting = false;
    if (atDone) {
      if (responseOverflow) Serial.println(F("AT RESPONSE TRUNCATED: treated as failure."));
      return atOK && !responseOverflow &&
             (!expctAns || !expctAns[0] || strstr(response, expctAns));
    }
    Serial.println(F("AT TIMEOUT: result unknown."));
    if (!startupProbe) { modemNeedsReset = true; return false; }
  }
  modemNeedsReset = true;
  return false;
}

void CBCread() {
  // get GNSS data
  if (sendATcmd(F("AT+CBC"), "OK", 5000)) {

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

  bool ret = false;

  dataStr = "AT+HTTPPARA=\"URL\",\"http://www.bosl.com.au/IoT/";
  dataStr += SITE_DIR;
  dataStr += "/scripts/WriteMe_v2.php?SiteName=";
  dataStr += SITE_ID;
  dataStr += ".csv";
  dataStr += "&Temp=";
  dataStr += Temp; // temperature of the low cost sensor
  dataStr += "&Depth=";
  dataStr += Press; // pressure of the low cost sensor --> need to convert
                    // to water level based on temperature data
  dataStr += "&EC=";
  dataStr += EC; // EC data --> need to check the sensor's code to fix
  dataStr += "&Turbw=";
  dataStr += turbidity; // Turbidity with LED
  dataStr += "&Turbwo=";
  dataStr += turbiditywoled; // Turbidity without LED
  dataStr += "&Batt=";
  dataStr += CBC;
  dataStr += "&CSQ=";
  dataStr += CSQ;
  dataStr += "\"";


  if (modemNeedsReset || httpActionPending) return false;
  if (!sendATcmd(F("AT+HTTPINIT"),"OK",5000,1)) {
    // Clean a stale HTTP session, but never issue commands after a timeout.
    if (modemNeedsReset) return false;
    if (!sendATcmd(F("AT+HTTPTERM"),"OK",5000,1) ||
        !sendATcmd(F("AT+HTTPINIT"),"OK",5000,1)) return false;
  }
  if (sendATcmd(F("AT+HTTPPARA=\"CID\",1"),"OK",5000,1) &&
      sendATcmd(dataStr,"OK",5000,1)) {
    pollModem();
    httpActionSeen=false; httpStatusCode=-1; httpResponseBytes=0;
    httpActionPending=true; // Arm BEFORE command: immediate URCs are not lost.
    if (sendATcmd(F("AT+HTTPACTION=0"),"OK",5000,1)) {
      const uint32_t start=millis();
      while (!httpActionSeen && (uint32_t)(millis()-start)<HTTP_TIMEOUT_MS) {
        pollModem(); delay(1);
      }
      if (httpActionSeen) {
        Serial.print(F("HTTP status: ")); Serial.print(httpStatusCode);
        Serial.print(F("; response bytes: ")); Serial.println(httpResponseBytes);
        ret=httpStatusCode==200;
      } else {
        Serial.println(F("HTTP completion unknown: no immediate retry (duplicate-write risk)."));
        modemNeedsReset=true; // Do not overlap HTTPTERM or another upload with this action.
      }
    } else if (!modemNeedsReset) httpActionPending=false; // Explicit command error, no pending action.
  }
  if (!modemNeedsReset && !httpActionPending)
    sendATcmd(F("AT+HTTPTERM"),"OK",5000,1);
  return ret;
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
