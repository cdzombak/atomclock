/**
 * Blink
 *
 * Turns on an LED on for one second,
 * then off for one second, repeatedly.
 */
#include "Arduino.h"

#define SERIAL_BAUD 115200


#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

// The shield uses the I2C SCL and SDA pins. On classic Arduinos
// this is Analog 4 and 5 so you can't use those for analogRead() anymore
// However, you can connect other I2C sensors to the I2C bus and share
// the I2C bus.
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

// These #defines make it easy to set the backlight color
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7


volatile bool tick = false;
bool led_state = false;
int ticks = 0;

IRAM_ATTR void tick_cb() {
  tick = true;
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D5, INPUT);
  pinMode(D6, INPUT);
  pinMode(D7, INPUT);

  Serial.begin(SERIAL_BAUD);

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.print("Hello, world!");
  lcd.setBacklight(WHITE);

  attachInterrupt(digitalPinToInterrupt(D6), tick_cb, FALLING);

  delay(1000);

  Serial.println("starting");

  if (digitalRead(D5) ==  LOW) {
    Serial.println("lock good");
  } else {
    Serial.println("no lock");
  }
  if (digitalRead(D7) ==  LOW) {
    Serial.println("service needed");
  } else {
    Serial.println("svc ok");
  }
}

void loop()
{
  if (tick) {
    Serial.print(millis());
    Serial.println(" tick");
    tick = false;
    ticks++;
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);

    if (digitalRead(D5) ==  LOW) {
      Serial.println("lock good");
    } else {
      Serial.println("no lock");
    }
    if (digitalRead(D7) ==  LOW) {
      Serial.println("service needed");
    } else {
      Serial.println("svc ok");
    }
  }
}
