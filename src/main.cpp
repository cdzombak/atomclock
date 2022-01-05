#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <time.h> // NOLINT(modernize-deprecated-headers)
#include <TZ.h>
extern "C" int _gettimeofday_r(struct _reent* unused, struct timeval *tp, void *tzp);

/* Hardware Configuration */

#define SERIAL_BAUD 115200
#define LOCK_PIN D5
#define TICK_PIN D6
#define SVC_PIN  D7
#define TICK_EDGE FALLING // rising just doesn't work with this particular microcontroller
// #define OSC_IGNORE_LOCK
// #define OSC_FORCE_SERVICE

/* User Configuration */
#include "config.h"
#ifndef CFG_HOSTNAME
#define CFG_HOSTNAME "atomclock"
#endif
#ifndef CFG_TZ
#define CFG_TZ TZ_America_Detroit
#endif
#ifndef CFG_TZ_STDTIME_DESC_3CHR
#define CFG_TZ_STDTIME_DESC_3CHR "EST"
#endif
#ifndef CFG_TZ_DST_DESC_3CHR
#define CFG_TZ_DST_DESC_3CHR "EDT"
#endif
#ifndef CFG_UPTIME_DISP_S
#define CFG_UPTIME_DISP_S (5)
#endif
#ifndef CFG_UTC_DISP_S
#define CFG_UTC_DISP_S (20)
#endif
// #define PRINT_TIME_TO_SERIAL

/* Display */

#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7

/* State Machine / State Management Definitions */

enum ClockState {
  LOCK_WAIT,  // waiting for oscillator lock
  WIFI_WAIT,  // waiting for WiFi connection
  SYNC_WAIT,  // waiting for sync with NTP
  READY       // oscillator locked, time sync'd, WiFi disconnected, and ready to display the time
};

enum LCDState {
  STARTING,    // Starting...
  LOCK,        // Oscillator Locking...
  WIFI,        // WiFi Connecting...
  SYNC,        // Time Syncing...
  TIME         // Time, with accessory information (date, uptime, UTC, service indicator)
};

ClockState clockState = LOCK_WAIT;
LCDState lcdState = STARTING;
unsigned long lastLcdTimeUpdate = 0;
bool ledState = true;
time_t upAt = 0;

volatile bool tick = false;
struct timeval oscTime;

/* Helper Functions */

#define SECS_PER_MIN (60)
#define SECS_PER_HR (SECS_PER_MIN * 60)
#define SECS_PER_DAY (SECS_PER_HR * 24)
#define SECS_PER_MONTH (SECS_PER_DAY * 30)
#define SECS_PER_YR (31536000)

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

IRAM_ATTR void tickISR() {
  if (clockState == READY) {
    if (oscTime.tv_sec == 0) {
      _gettimeofday_r(nullptr, &oscTime, nullptr);
    } else {
      oscTime.tv_sec += 1;
    }
    tick = true;
  }
}

inline bool oscLocked() __attribute__((always_inline));
bool oscLocked() {
#ifdef OSC_IGNORE_LOCK
  return true;
#else
  // digital LOW == good oscillator lock
  return digitalRead(LOCK_PIN) == LOW;
#endif
}

inline bool oscServiceIndicated() __attribute__((always_inline));
bool oscServiceIndicated() {
#ifdef OSC_FORCE_SERVICE
  return true;
#else
  // digital LOW == oscillator needs service
  return digitalRead(SVC_PIN) == LOW;
#endif
}

/* State Machine Implementations */

void updateLcd(LCDState desiredState) {
  switch(desiredState) {
    case STARTING:
      if (lcdState == STARTING) {
        return;
      }
      lcd.setBacklight(RED);
      lcd.setCursor(0,0);
      lcd.print("  Starting...   ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      lcdState = STARTING;
      break;

    case LOCK:
      if (lcdState == LOCK) {
        return;
      }
      lcd.setBacklight(RED);
      lcd.setCursor(0,0);
      lcd.print("   Oscillator   ");
      lcd.setCursor(0,1);
      lcd.print("   Locking...   ");
      lcdState = LOCK;
      break;

    case WIFI:
      if (lcdState == WIFI) {
        return;
      }
      lcd.setBacklight(YELLOW);
      lcd.setCursor(0,0);
      lcd.print("      WiFi      ");
      lcd.setCursor(0,1);
      lcd.print("  Connecting... ");
      lcdState = WIFI;
      break;

    case SYNC:
      if (lcdState == SYNC) {
        return;
      }
      lcd.setBacklight(YELLOW);
      lcd.setCursor(0,0);
      lcd.print("      Time      ");
      lcd.setCursor(0,1);
      lcd.print("    Syncing...  ");
      lcdState = SYNC;
      break;

    case TIME:
      if (lcdState != TIME) {
        lcd.clear();
        lcd.setBacklight(WHITE);
      }
      if (lcdState != TIME || tick || lastLcdTimeUpdate + 50 <= millis()) {
        lastLcdTimeUpdate = millis();
        time_t now = time(nullptr);
        struct tm timeinfo; // NOLINT(cppcoreguidelines-pro-type-member-init)
        localtime_r(&now, &timeinfo);
        char row0[17];
        char *tz;
        if (timeinfo.tm_isdst) {
          tz = CFG_TZ_DST_DESC_3CHR;
        } else {
          tz = CFG_TZ_STDTIME_DESC_3CHR;
        }
        sprintf(row0, "  %02d:%02d:%02d %.3s  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, tz);
        row0[16] = 0;
        lcd.setCursor(0,0);
        lcd.print(row0);

        lcd.setCursor(0,1);
        if (oscServiceIndicated()) {
          lcd.setBacklight(VIOLET);
          lcd.print("service required");
        } else if (timeinfo.tm_sec < CFG_UPTIME_DISP_S && upAt != 0) {
          char row1[17];
          time_t upSec = now - upAt;
          if (upSec >= (2*SECS_PER_YR)) {
            sprintf(row1, "  up: %2d years  ", (int)(upSec/SECS_PER_YR));
          } else if (upSec >= (2*SECS_PER_MONTH)) {
            sprintf(row1, "  up %2d months  ", (int)(upSec/SECS_PER_MONTH));
          } else if (upSec >= (3*SECS_PER_DAY)) {
            sprintf(row1, "   up %2d days   ", (int)(upSec/SECS_PER_DAY));
          } else if (upSec >= SECS_PER_HR) {
            sprintf(row1, "  up: %2d hours  ", (int)(upSec/SECS_PER_HR));
          } else if (upSec >= SECS_PER_MIN) {
            sprintf(row1, " up: %2d minutes ", (int)(upSec/SECS_PER_MIN));
          } else {
            sprintf(row1, " up: %2d seconds ", (int)(upSec));
          }
          row1[16] = 0;
          lcd.print(row1);
        } else if (timeinfo.tm_sec < CFG_UTC_DISP_S) {
          gmtime_r(&now, &timeinfo);
          char row1[17];
          sprintf(row1, "  %02d:%02d:%02d UTC  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
          row1[16] = 0;
          lcd.print(row1);
        } else {
          char *day = "---";
          char *month = "---";
          switch(timeinfo.tm_wday) {
            case 0:
              day = "Sun";
              break;
            case 1:
              day = "Mon";
              break;
            case 2:
              day = "Tue";
              break;
            case 3:
              day = "Wed";
              break;
            case 4:
              day = "Thu";
              break;
            case 5:
              day = "Fri";
              break;
            case 6:
              day = "Sat";
              break;
          }
          switch(timeinfo.tm_mon) {
            case 0:
              month = "Jan";
              break;
            case 1:
              month = "Feb";
              break;
            case 2:
              month = "Mar";
              break;
            case 3:
              month = "Apr";
              break;
            case 4:
              month = "May";
              break;
            case 5:
              month = "Jun";
              break;
            case 6:
              month = "Jul";
              break;
            case 7:
              month = "Aug";
              break;
            case 8:
              month = "Sep";
              break;
            case 9:
              month = "Oct";
              break;
            case 10:
              month = "Nov";
              break;
            case 11:
              month = "Dec";
              break;
          }
          char row1[17];
          #pragma GCC diagnostic push
          #pragma GCC diagnostic ignored "-Wformat-overflow="
          sprintf(row1, "%.3s, %.3s %.2d %.4d", day, month, timeinfo.tm_mday, min(1900+timeinfo.tm_year, 9999));
          #pragma GCC diagnostic pop
          row1[16] = 0;
          lcd.print(row1);
        }
      }
      lcdState = TIME;
      break;
  }
}

void verifyLock() {
  if (!oscLocked()) {
    clockState = LOCK_WAIT;
    oscTime.tv_sec = 0;
    oscTime.tv_usec = 0;
    lastLcdTimeUpdate = 0;
    upAt = 0;
    tick = false;
    Serial.printf_P(PSTR("Waiting for oscillator lock.\r\n"));
  }
}

void transitionToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(CFG_HOSTNAME);
  WiFi.begin(CFG_WIFI_ESSID, CFG_WIFI_PASSWORD);
  clockState = WIFI_WAIT;
  yield();
}

void transitionToSync() {
  configTime(CFG_TZ, "pool.ntp.org", "time.nist.gov");
  clockState = SYNC_WAIT;
  yield();
}

/* Main */

void setup() {
  oscTime.tv_sec = 0;
  oscTime.tv_usec = 0;

  Serial.begin(SERIAL_BAUD);
  lcd.begin(16, 2);
  updateLcd(STARTING);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, ledState);
  pinMode(LOCK_PIN, INPUT);
  pinMode(SVC_PIN, INPUT);
  pinMode(TICK_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TICK_PIN), tickISR, TICK_EDGE);

  Serial.printf_P(PSTR("Initialized.\r\n"));
#ifdef OSC_IGNORE_LOCK
  Serial.printf_P(PSTR("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n"));
  Serial.printf_P(PSTR("WARNING: Oscillator lock indicator is ignored.\r\n"));
  Serial.printf_P(PSTR("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n"));
#endif
#ifdef OSC_FORCE_SERVICE
  Serial.printf_P(PSTR("WARNING: Oscillator service indicator is forced on.\r\n"));
#endif
  if (CFG_UTC_DISP_S <= CFG_UPTIME_DISP_S) {
    Serial.printf_P(PSTR("WARNING: CFG_UTC_DISP_S <= CFG_UPTIME_DISP_S; UTC time will not be displayed."));
  }
  Serial.printf_P(PSTR("Waiting for oscillator lock.\r\n"));

  // Slight delay after power-on to allow oscillator to drive lock/svc outputs:
  // This prevents momentarily advancing from LOCK_WAIT to WIFI_WAIT immediately after
  // power-on, and could also be addressed with a high-value pull-up
  // resistor on the LOCK signal at the ESP8266.
  delay(1000);
}

void loop() {
  switch(clockState) {
    case LOCK_WAIT:
      if (oscLocked()) {
        Serial.printf_P(PSTR("Connecting to WiFi (%s)\r\n"), CFG_WIFI_ESSID);
        transitionToWifi();
      } else {
        updateLcd(LOCK);
      }
      break;

    case WIFI_WAIT:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("WiFi connected. My IP: %s\r\n"), WiFi.localIP().toString().c_str());
        Serial.printf_P(PSTR("Initiating NTP time sync.\r\n"));
        transitionToSync();
      } else {
        updateLcd(WIFI);
      }
      verifyLock();
      break;

    case SYNC_WAIT:
      if (time(nullptr) > (time_t)(1638825994)) {
        clockState = READY;
        WiFi.disconnect(true);
        Serial.printf_P(PSTR("Time synced.\r\n"));
      } else if (WiFi.status() != WL_CONNECTED) {
        transitionToWifi();
      } else {
        updateLcd(SYNC);
      }
      verifyLock();
      break;

    case READY:
      if (tick) {
        settimeofday(&oscTime, nullptr);
      }
      updateLcd(TIME);
      if (tick) {
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
        if (upAt == 0) {
          upAt = time(nullptr);
        }
        tick = false;
        #ifdef PRINT_TIME_TO_SERIAL
        time_t now = time(nullptr);
        struct tm timeinfo; // NOLINT(cppcoreguidelines-pro-type-member-init)
        localtime_r(&now, &timeinfo);
        Serial.printf_P(PSTR("Current time (local): %s"), asctime(&timeinfo));
        gmtime_r(&now, &timeinfo);
        Serial.printf_P(PSTR("Current time (UTC):   %s"), asctime(&timeinfo));
        #endif
      }
      verifyLock();
      break;
  }
}
