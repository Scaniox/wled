#pragma once
/*
 * Usermod: Dual-Wake Deep Sleep
 * ------------------------------
 * Puts the board into deep sleep and wakes when EITHER of two GPIOs
 * goes HIGH.
 *
 * Supported:
 *   - Classic ESP32, ESP32-S2, ESP32-S3  -> uses EXT1 wakeup (ANY_HIGH)
 *   - ESP32-C3 / C2 / C6                 -> uses generic GPIO deep-sleep wakeup
 *   - ESP8266 is NOT supported: it has no external-GPIO deep-sleep wake path.
 *
 * Notes:
 *   - EXT1 pins on classic ESP32 must be RTC-capable:
 *     0, 2, 4, 12-15, 25-27, 32-39
 *   - Add an external pulldown resistor on each wake pin so it reads a
 *     clean LOW when idle. Internal pulls aren't reliably retained by the
 *     RTC domain across deep sleep on all ESP32 variants.
 *   - This usermod does NOT decide *when* to sleep - call armSleep() from
 *     your own trigger logic (inactivity timeout, third input, MQTT, etc).
 *
 * Integration:
 *   1. Drop this file in usermods/dualwake_deepsleep/
 *   2. In usermods_list.cpp add:
 *        #include "../usermods/dualwake_deepsleep/usermod_dualwake_deepsleep.h"
 *      and inside registerUsermods():
 *        usermods.add(new UsermodDualWakeDeepSleep());
 *   3. Pins/timing are configurable at runtime via Usermods config in the
 *      WLED UI, or set WAKE_PIN_1 / WAKE_PIN_2 defines below as defaults.
 */

#include "wled.h"

// #ifndef WAKE_PIN_1
// #define WAKE_PIN_1 4   //- maps to left indicat0
// #endif
// #ifndef WAKE_PIN_2
// #define WAKE_PIN_2 34 // - maps to IGN input
// #endif

#define MAX_WAKE_PINS 6

static bool isClassicEsp32RtcPin(int8_t pin) {
  switch (pin) {
    case 0: case 2: case 4:
    case 12: case 13: case 14: case 15:
    case 25: case 26: case 27:
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
      return true;
    default:
      return false;
  }
}


class UsermodDualWakeDeepSleep : public Usermod {
  private:
    bool     sleepArmed  = true;
    uint32_t armedAt     = 0;
    int8_t   wakePins[MAX_WAKE_PINS]        = {16, 4, 27, 23, 22, 34};
    
    // configurable settings
    bool     enabled     = true;
    uint32_t armDelayMs  = 15000;   // grace period before actually sleeping
    int8_t   relayPin   = 5;
    bool relayActiveLow = false;

    static const char _name[];

  public:
    void setup() override {
      for (uint8_t i = 0; i < MAX_WAKE_PINS; i++) {
        if (!isClassicEsp32RtcPin(wakePins[i])) continue;
        pinMode(wakePins[i], INPUT);
      }
      setRelay(true);
      armedAt = millis();
      reportWakeCause();
    }

    void loop() override {
      if (!enabled || !sleepArmed || strip.isUpdating()) return;
      
      for (uint8_t i = 0; i < MAX_WAKE_PINS; i++) {
        if (!isClassicEsp32RtcPin(wakePins[i])) continue;
        if (digitalRead(wakePins[i])) {
          armedAt = millis();
          return;
        }
      }

      if (millis() - armedAt > armDelayMs) {
        enterDeepSleep();
      }
    }

    // disarm sleep when updating sleep when 
    void onUpdateBegin(bool init) {
      if (init) sleepArmed = false;
      else sleepArmed = true;

      armedAt = millis();
    }

    // Call this from wherever your own logic decides it's time to sleep,
    // e.g. an inactivity timer or a third trigger.
    void armSleep() {
      sleepArmed = true;
      armedAt = millis();
    }

    void setRelay(bool active) {
      if(relayPin != -1) {
        pinMode(relayPin, OUTPUT);
        digitalWrite(relayPin, active ^ relayActiveLow);
      }
    }

    void reportWakeCause() {
      esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
      switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT1: {
          #if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C2) && !defined(CONFIG_IDF_TARGET_ESP32C6)
          uint64_t mask = esp_sleep_get_ext1_wakeup_status();
          DEBUG_PRINTF("[DualWake] Woke via EXT1, pin mask: 0x%llx\n", mask);
          #endif
          break;
        }
        case ESP_SLEEP_WAKEUP_GPIO:
          DEBUG_PRINTLN(F("[DualWake] Woke via GPIO wakeup"));
          break;
        default:
          DEBUG_PRINTLN(F("[DualWake] Normal boot (not a deep-sleep wake)"));
          break;
      }
    }

    void enterDeepSleep() {
      DEBUG_PRINTLN(F("[DualWake] Entering deep sleep - wake on pin1 OR pin2 HIGH"));
      Serial.flush();

      uint64_t mask = 0;
      for (uint8_t i = 0; i < MAX_WAKE_PINS; i++) {
        if (wakePins[i] >= 0 && isClassicEsp32RtcPin(wakePins[i])) mask |= (1ULL << wakePins[i]);
      }

      #if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32C6)
        // C-series: no ext0/ext1 - generic GPIO deep sleep wake
        // (requires arduino-esp32 core >= 2.0.4 / IDF >= 4.4)
        esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_HIGH);
      #else
        // Classic ESP32 / S2 / S3: EXT1 with ANY_HIGH ORs the pins together
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
      #endif

      delay(100);
      setRelay(false);
      esp_deep_sleep_start();
    }

    void addToConfig(JsonObject& root) override {
      armedAt = millis(); // reset arm timer so enabling doesn't cause instant off

      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["enabled"]    = enabled;
      top["armDelayMs"] = armDelayMs;
      top["relay pin"] = relayPin;
      top["relay active low"] = relayActiveLow;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top["enabled"],    enabled,    enabled);
      configComplete &= getJsonValue(top["armDelayMs"], armDelayMs, armDelayMs);
      configComplete &= getJsonValue(top["relay pin"], relayPin, -1);
      configComplete &= getJsonValue(top["relay active low"], relayActiveLow, relayActiveLow);

      setRelay(true);
      return configComplete;
    }

    uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

const char UsermodDualWakeDeepSleep::_name[] PROGMEM = "DualWakeDeepSleep";

static UsermodDualWakeDeepSleep umdds;
REGISTER_USERMOD(umdds);