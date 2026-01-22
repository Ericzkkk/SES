#define APN "simbase"
#define MCCMNC "50501"
#define BAUDRATE 9600 // talk to SIM7000 baud rate
#define CHARBUFF 254  // SIM7000 serial response buffer,longer than 255 will cause issues

#define NetworkSignal_Search false      // True: We areadly know a good MCCMNC, false, unknown area: skil straight to auto scan
#define LOG_TO_WEB true                 // logging to web only occurs when true. if false SD log times may not be accurate
#define ALLOW_GSM false                 // most networks have disabled GSM
#define ALLOW_NBIOT false               // NB-IoT requires simcard support
#define ACCEPT_NON_ROAMING true         // set false to only accept roaming connections

#define checktime  5000

#include <MCP7940.h>
#include <SD.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>

// ********* Configurations for error handling ************
#define HTTP_FAILS_RESET_THRESHOLD       (240) //how many HTTP fails are needed to trigger a sim7000 reset
#define TRY_POWER_ON_MAX_RESETS          (3)   //max number of reset attempts when attempting to power on the sim7000

#define POWER_ON_FAILS_RESET_LIMIT_BEGIN (6)   //how many power on resets are needed to rate limit the reset attempts
#define POWER_ON_FAILS_RESET_LIMIT_RATE  (30)  //in the rate limit mode, this is the number of reset requests needed before a reset is actually performed.

#define NET_REG_FAILS_LIMIT_BEGIN (12)   //how many power on resets are needed to rate limit the reset attempts
#define NET_REG_FAILS_LIMIT_RATE  (120)  //in the rate limit mode, this is the number of reset requests needed before a reset is actually performed.

// *** special macro ***
#define MAX_OPERATORS (8)
#define POWER_ON_TRY_TIMES 5
#define HTTP_UPLOAD_TRY_TIMES 3 // to fix the issues of simcom or bad signal.
// first time try power off simcom, second time try reset simcom, third time
// and forward only try power off simcom to avoid frequent reset
#define simCom Serial1

char CSQ[5];
char response[CHARBUFF]; // sim7000 serial response buffer
// atmega timer0_millis
extern volatile unsigned long timer0_millis;

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

static LiquidCrystal_I2C* g_lcd = nullptr;
static bool g_has_lcd = false;

// Fill this with the addresses of other I²C sensors you already have, to skip collisions
static const uint8_t OTHER_I2C_ADDRS[] = { /* e.g., 0x49, 0x4A, 0x4B, 0x76, 0x44 */ };

static bool addr_in_list(uint8_t addr, const uint8_t* list, size_t n) {
  for (size_t i = 0; i < n; i++) if (list[i] == addr) return true;
  return false;
}

// Optional: quick scanner for debugging
static void i2c_scan_print() {
  for (uint8_t a = 0x03; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("I2C found @0x")); Serial.println(a, HEX);
    }
  }
}
// Print helpers (unchanged usage)
static void ui_status(const __FlashStringHelper* line1, const char* line2 = nullptr) {
  Serial.println(line1);
  if (line2) Serial.println(line2);
  if (!g_has_lcd) return;
  g_lcd->clear();
  g_lcd->setCursor(0, 0);
  char buf1[17] = {0}; strncpy_P(buf1, (PGM_P)line1, 16); g_lcd->print(buf1);
  if (line2 && *line2) { g_lcd->setCursor(0, 1); char buf2[17]={0}; strncpy(buf2, line2, 16); g_lcd->print(buf2); }
}

static void ui_status2(const char* line1, const char* line2 = nullptr) {
  Serial.println(line1);
  if (line2) Serial.println(line2);
  if (!g_has_lcd) return;
  g_lcd->clear();
  g_lcd->setCursor(0, 0); g_lcd->print(String(line1).substring(0,16));
  if (line2 && *line2) { g_lcd->setCursor(0, 1); g_lcd->print(String(line2).substring(0,16)); }
}
// Show current RAT/PLMN/signal on Serial and LCD (if present)
static void showCurrentNetwork() {
  char rat[24] = {0};
  char plmn[7] = {0};
  uint8_t csq;

  getRatFromCPSI(rat, sizeof(rat));    // e.g., "LTE CAT-M1"
  getMccmnc(plmn, sizeof(plmn));       // numeric MCCMNC
  getRSSI(&csq);                       // 0..31 (99=unknown)

  // ----- Serial -----
  Serial.println(F("=== Current Network ==="));
  Serial.print(F("Server Type : ")); Serial.println(rat[0] ? rat : "Unknown");
  Serial.print(F("MCCMNC      : ")); Serial.println(plmn);
  Serial.print(F("Signal (CSQ): ")); Serial.println(csq);
  Serial.println(F("======================="));

  // ----- LCD (16x2 safe) -----
  char l1[17]; snprintf(l1, sizeof(l1), "%.16s", rat);
  char l2[17]; snprintf(l2, sizeof(l2), "%s CSQ:%u", plmn, csq);
  ui_status2(l1, l2);
}

static bool lcd_autodetect_init(uint8_t cols = 16, uint8_t rows = 2) {
  Wire.begin();
  Wire.setClock(100000); // safer with multiple devices

  // i2c_scan_print();  // ← uncomment if you want to see everything on the bus

  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    if (addr_in_list(addr, OTHER_I2C_ADDRS, sizeof(OTHER_I2C_ADDRS))) continue;

    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;  // no ACK

    LiquidCrystal_I2C* lcd = new LiquidCrystal_I2C(addr, cols, rows);
    lcd->init();
    lcd->backlight();
    lcd->clear();
    lcd->setCursor(0, 0); lcd->print("LCD @0x");
    char hexbuf[5]; snprintf(hexbuf, sizeof(hexbuf), "%02X", addr);
    lcd->print(hexbuf);
    lcd->setCursor(0, 1); lcd->print("Ready");

    g_lcd = lcd;
    g_has_lcd = true;
    return true;
  }
  g_has_lcd = false;
  return false;
}



void CSQread() {
  uint8_t csq;
  getRSSI(&csq);
  memset(CSQ, '\0', sizeof(CSQ));
  sprintf(CSQ, "%" PRIu8, csq);
}

int getRSSI(uint8_t* rssi_p){
  *rssi_p = 99;

  if (!sendATcmd(F("AT+CSQ"), "OK", 1000, 1)) {
    return false;
  }

  char* start = strchr(response, ':'); // Look for the colon

  if (start == NULL) return false;

  int temp_rssi;
  // %*d tells sscanf to read the second number but ignore it
  int ret = sscanf(start, ": %d,%*d", &temp_rssi);

  if (ret == 1) {
    *rssi_p = (uint8_t)temp_rssi; // Cast it back to 8-bit
    return true;
  }

  return false;
}

void netUnreg() { 
  //sendATcmd(F("AT+CFUN=0"), "OK", 1000); 
  // 1. Terminate any active IP connections
  sendATcmd(F("AT+CIPSHUT"), "SHUT OK", 2000, 1);
  
  // 2. De-register from the operator
  sendATcmd(F("AT+COPS=2"), "OK", 5000, 1);
  
  // 3. Enter Airplane mode for a clean RF state
  sendATcmd(F("AT+CFUN=4"), "OK", 2000, 1);
  Serial.println(F("Modem is now Offline."));
  }

int isRegistered(uint32_t timeout_ms = 30000) { // Default to 30 seconds
  uint32_t start_time = millis();
  
  // Keep trying until the timeout is reached
  while (millis() - start_time < timeout_ms) {
    
    // Check for EPS (LTE) registration
    if (sendATcmd(F("AT+CEREG?"), "OK", 1000, 1)) {
      
      // Better check: look for ,1 (Home) or ,5 (Roaming) 
      // This avoids being tricked by the <n> value at the start
      if (strstr(response, ",1") || strstr(response, ",5")) {
        Serial.println(F("CEREG: registered"));
        showCurrentNetwork();
        return true;
      }
    }

    Serial.println(F("Waiting for registration..."));
    
    // Give the modem time to breathe (at least 1-2 seconds)
    // Spamming AT commands can actually slow down the registration process
    delay(2000); 
  }

  Serial.println(F("CEREG: Timeout - Not registered"));
  return false;
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

  // Power up the RF
  if (sendATcmd(F("AT+CFUN=1"), "OK", 10000, 2) == false) { // enable the modem
    return false;
  }

  // GSM allowed or LTE-only
  if(ALLOW_GSM){
    sendATcmd(F("AT+CNMP=2"), "OK",
              500, 1); //enable GSM
  }else{
    sendATcmd(F("AT+CNMP=38"), "OK",
              500, 1); //disable GSM, allow LTE-M
  }

  // NB-IoT enable/disable
  if(ALLOW_NBIOT){
    sendATcmd(F("AT+CMNB=3"), "OK",
               500, 1); //enable NB-IoT
  }else{
    sendATcmd(F("AT+CMNB=1"), "OK",
               500, 1); //disable NB-IoT (simbase does not offer NB-IoT)
  }

  // PDP profile
  sendATcmd(F("AT+CGDCONT=1,\"IP\",\"" APN "\""), "OK",
            2000); // set IPv4 and apn

  // Quick Test to see if is already registered
  //ret = isRegistered();

  //if(ret){return ret;}

  if (NetworkSignal_Search) {

  // 1) Try preferred network MCCMNC we cached before (user's last good)
  ret = netSelect(mccmnc);
  
  if(ret){
    ret = getMccmnc(mccmnc, sizeof(mccmnc));
    if(ret){
      Serial.print(F("Updating network preference to: "));
      Serial.println(mccmnc);
    }
    return true;
  }
  
  } else {

  return try_auto_search_rssi_then_bands(mccmnc,sizeof(mccmnc),&network_searched);

  }
}

static bool try_auto_search_rssi_then_bands(char* mccmnc,size_t mccmnc_len, uint8_t* network_searched) {
  
  int ret;

  Serial.println(F("Start Auto Searching Networks, this would take 10 mins."));
  // Step 4: Do the long operator search once (slow)
  if (!(*network_searched)) {
      Serial.println(F("Starting deep scan (~10 mins)..."));
      netSearch(); 
      *network_searched = 1; 
    }

    Serial.print(F("Found operators: "));
    Serial.println(operators_len);

    for(uint8_t i = 0; i < operators_len; i++){
      Operator* o = &operators[i];
      
      // Corrected variable reference
      Serial.print(F("Checking Operator: "));
      Serial.print(o->mccmnc);
      Serial.print(F(" RSSI: "));
      Serial.println(o->rssi);

      ret = netSelect(o->mccmnc);
      if(ret) {
        // Use the passed length instead of sizeof()
        ret = getMccmnc(mccmnc, mccmnc_len); 
        if(ret) {
          Serial.print(F("Successfully connected to: "));
          Serial.println(mccmnc);
          return true;
        }
      }
    }

  Serial.println(F("Unable to connect to network"));
  return false;
}



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
    end   = strstr(start, "),") + 1;//end of an operator listing
    if(end == NULL){return false;}

    len = end - start;//check that the matches are of reasonable length
    if(len > CHARBUFF){return false;}

    Operator* o = &operators[operators_len];
    uint8_t matches = sscanf(start, "(%d,%*[^,],%*[^,],\"%6[^\"]\",%d)", &o->status, &o->mccmnc, &o->netact);
    if(matches == 3){
      o->rssi = 99;//not known or not detectable
      operators_len++;
        }
else {return false;}

    start = end;
  }
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

int compare_rssi(const void *a, const void *b){
  int16_t rssi_a = ((Operator*)a)->rssi;
  int16_t rssi_b = ((Operator*)b)->rssi;

  if(rssi_a == 99 && rssi_b == 99){return 0;}
  if(rssi_a == 99){return 1;}
  if(rssi_b == 99){return -1;}

  return  rssi_a > rssi_b ? -1 : 
           (rssi_b > rssi_a ?  1 : 0);

}

static bool getRatFromCPSI(char* out, size_t outlen) {
  if (!sendATcmd(F("AT+CPSI?"), "OK", 2000, 1)) { out[0] = '\0'; return false; }
  char* p = strstr(response, "+CPSI:");
  if (!p) { out[0] = '\0'; return false; }
  p += 7; // after "+CPSI: "
  char* comma = strchr(p, ',');
  if (!comma) { out[0] = '\0'; return false; }
  size_t n = (size_t)(comma - p);
  if (n >= outlen) n = outlen - 1;
  strncpy(out, p, n);
  out[n] = '\0';
  return true;
}

void updateLiveStatus() {
  char rat[24] = {0};
  char mccmnc_current[10] = {0};
  uint8_t csq = 99;

// 1. Get the data from the modem
  getRatFromCPSI(rat, sizeof(rat));
  getMccmnc(mccmnc_current, sizeof(mccmnc_current));
  getRSSI(&csq);

  char l1[17]; snprintf(l1, sizeof(l1), "%.16s", rat);           // e.g., "LTE CAT-M1"
  char l2[17]; snprintf(l2, sizeof(l2), "%s CSQ:%u", mccmnc_current, csq);
  ui_status2(l1, l2);

  // (keep your Serial summary too)
  Serial.println(F("=== Final Selection ==="));
  Serial.print(F("Server Type : ")); Serial.println(rat[0] ? rat : "Unknown");
  Serial.print(F("MCCMNC      : ")); Serial.println(mccmnc_current);
  Serial.print(F("Signal (CSQ): ")); Serial.println(csq);
  Serial.println(F("=======================\n"));
} 

int netSearch(void){
  int ret = false;

  // 1. Initial Setup
  ui_status(F("Searching..."),"MCCMNC...");  
  sendATcmd(F("AT+COPS=3,2"), "OK", 1500, 1); // numeric format for COPS

  // 2. Start Search for all bands currently allowed
  ret = sendATcmd(F("AT+COPS=?"), "OK", 600000, 1); //10 mins for a quick search

  if(!ret){ ui_status(F("Scan failed")); return false; }

  // 3. Parse the modem's raw list 
  storeOperators();
  _dbgPrintOperators();

  // 4. If nothing parsed, try auto attach and finish with summary
  if (operators_len == 0) {
  Serial.println(F("Scan returned 0 operators. Trying Auto-Attach..."));
  ui_status(F("Auto Attach"));

    if (netSelect(NULL)) { // auto
    updateLiveStatus();
      return true;
    }
    Serial.println(F("Auto-Attach failed. No signal available."));
    return false;
  }

  char line2[17]; snprintf(line2, sizeof(line2), "Found:%u", operators_len);
  ui_status(F("Scan done"), line2);

  // 5. SORT: Rank the best towers by RSSI first
  ui_status(F("Sort the best..."));   
  Serial.println("Rank strongest");
  // Rank strongest first using your comparator
  qsort(operators, operators_len, sizeof(Operator), compare_rssi);

  // 6. Walk through each parsed operator, and try to register to that specific band
  for(uint8_t i = 0; i < operators_len; i++){
    Operator* o = &operators[i];

    char trybuf[17]; 
    snprintf(trybuf, sizeof(trybuf), "Try %s", o->mccmnc);
    ui_status(F("Register..."), trybuf);

  // Attempt Manual Selection (AT+COPS=1,2,"MCCMNC")
    ret = netSelect(o->mccmnc);
    if(!ret){
      ui_status(F("Reg failed"), o->mccmnc);
      continue;
    }

    // 7. VERIFICATION: Wait a moment and check if we are actually registered
    if (isRegistered(15000)) { 
      char mccmnc_current[7] = {0};
      getMccmnc(mccmnc_current, sizeof(mccmnc_current));
      // Verify the modem is actually on the network we requested
      if (strcmp(o->mccmnc, mccmnc_current) == 0) {
        ui_status(F("Reg OK"), mccmnc_current);
        updateLiveStatus(); 
        return true; 
      }
    }
    ui_status(F("Reject/Failed"), o->mccmnc);
    sendATcmd(F("AT+COPS=2"), "OK", 3000, 1); // Disconnect before next try

  // 9. FINAL FAILURE: If the loop finishes, no operator worked
  Serial.println(F("All scanned operators failed to connect."));
  ui_status(F("No Networks"));
  return false;
}
}

void setup() {
  // set up pins for sim7000
  pinMode(SIM_BUF_EN, OUTPUT);
  digitalWrite(SIM_BUF_EN, HIGH);

  pinMode(SIM_PWRKEY, OUTPUT);
  digitalWrite(SIM_PWRKEY, HIGH);

  pinMode(SIM_RESET, OUTPUT);
  digitalWrite(SIM_RESET, HIGH);

  pinMode(SIM_STATUS_BFD, INPUT);
  xDelay(1000); // we need to wait a little bit for the pin inputs to settle

  // begin serial and LCD screen
  Serial.begin(BAUDRATE);    
  lcd_autodetect_init(16, 2);                 // use (20,4) if you have a 20x4
  ui_status(F("LCD start"),"Scanning...");

  ui_status(F("Initial SIMCOM"));
  Serial.println(F("Initialising SIM 7000"));
  simCom.begin(BAUDRATE);

  static uint16_t power_on_fails = 0;
  uint8_t reset = false;
  // Code to start SIMCOM module
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

    if(tryPowerOnSimCom(POWER_ON_TRY_TIMES) == false){reset = true; continue;}

    if(serialBegin() == false){reset = true; continue;}

  }

  ui_status(F("Deregister"),"Networks");
  netUnreg();

  netReg();

}

void loop() {
  ui_status(F("Loop Start"));
    char current_mccmnc[10] = {0};
    uint8_t network_searched = 0;
// 1. CHECK STATUS: Are we already connected to 50501?
  ui_status(F("Status Check"));
if (isRegistered(10000)) { 
    if (getMccmnc(current_mccmnc, sizeof(current_mccmnc))) {
      Serial.print(F("Auto-Connected to: ")); Serial.println(current_mccmnc);
      lockAndMonitor(current_mccmnc);
    }
  }

// 2. SEARCH: If fast check fails, find the signal manually
  Serial.println(F("No valid connection. Start Searching all bands..."));
  
  if (try_auto_search_rssi_then_bands(current_mccmnc, sizeof(current_mccmnc), &network_searched)) {
    Serial.print(F("Found best signal on: ")); Serial.println(current_mccmnc);
    lockAndMonitor(current_mccmnc);
  } else {
    Serial.println(F("No network found. Retrying scan in 10s..."));
    delay(10000);
  }
}


 // openbearer();

 // CSQread();

 // closeBearer();



void lockAndMonitor(char* mccmnc) {
  Serial.println(F("Entering Signal Monitor Mode. Press RESET to scan for new locations."));
  
  while(true) {
    
    // Optional: If you lose registration, don't restart the 10-min scan 
    // automatically, just keep waiting/trying for that specific tower.
    if (!isRegistered(5000)) {
      Serial.println(F("Signal Lost. Refrsh the program..."));
      return;
    }

    updateLiveStatus(); // The function that prints RSSI bars

    delay(checktime); 
  }
}
