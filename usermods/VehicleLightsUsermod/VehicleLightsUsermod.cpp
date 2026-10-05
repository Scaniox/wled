// claude generated

#include "wled.h"

/*
 * Vehicle Lights usermod (indicators + DRL)
 *
 * Settings (Config -> Usermods) and cfg.json under "Vehicle Lights":
 *   left_ind_pin, r_ind_pin, drl_pin, transition_delay
 *   presets: l_on, l_off, l_clear, l_pwr_off, r_on, r_off, r_clear,
 *            r_pwr_off, drl_on, drl_off, drl_pwr_off
 *
 * Preset 0 = "do nothing". Pins are read as ACTIVE HIGH (use an optocoupler /
 * level shifter from the 12V vehicle signal, never connect 12V directly).
 *
 * Behaviour (per indicator side):
 *   pin goes HIGH                          -> "on" preset
 *   pin goes LOW                           -> "off" preset
 *   LOW for longer than transition_delay   -> "clear" preset
 *   ...then another transition_delay later, and only if DRL is inactive
 *                                          -> "pwr off" preset
 * DRL:
 *   pin goes HIGH                          -> "drl on" preset
 *   pin goes LOW                           -> "drl off" preset
 *   LOW for longer than transition_delay and no indicator active
 *                                          -> "drl pwr off" preset
 */


typedef enum {IND_ON, IND_OFF, IND_CLEAR} ind_state_e;

class VehicleLightsUsermod : public Usermod {
  private:
    static const char _name[];
    uint8_t index = 0; // 0 - left, 1 - right 2 - drl

    // config
    int8_t   leftPin  = -1;
    int8_t   rightPin = -1;
    int8_t   drlPin   = -1;
    uint16_t transitionDelay = 500;   // ms

    uint8_t pLeftOn = 0, pLeftOff = 0, pLeftClear = 0;
    uint8_t pRightOn = 0, pRightOff = 0, pRightClear = 0;
    uint8_t pDrlOn = 0, pDrlOff = 0;

    // runtime
    struct Channel {
      int8_t        pin = -1;
      bool          level = false;
      unsigned long lastChange = 0;
      ind_state_e stage = IND_CLEAR;
    };
    Channel left, right, drl;
    bool enabled = false;
    bool initDone = false;

    static void apply(uint8_t preset) {
      if (preset > 0) applyPreset(preset, CALL_MODE_DIRECT_CHANGE);
      handlePresets();
    }

    void releasePins() {
      int8_t pins[3] = { left.pin, right.pin, drl.pin };
      for (uint8_t i = 0; i < 3; i++) {
        if (pins[i] >= 0) PinManager::deallocatePin(pins[i], PinOwner::UM_Unspecified);
      }
      left.pin = right.pin = drl.pin = -1;
    }

    bool allocatePins() {
      Channel* ch[3]   = { &left, &right, &drl };
      int8_t   want[3] = { leftPin, rightPin, drlPin };
      for (uint8_t i = 0; i < 3; i++) {
        if (want[i] < 0) continue;
        if (!PinManager::allocatePin(want[i], false, PinOwner::UM_Unspecified)) {
          want[i] = -1;
          continue;
        }
        pinMode(want[i], INPUT_PULLDOWN);   // change to INPUT if pin has no pulldown
        ch[i]->pin        = want[i];
        ch[i]->level      = digitalRead(want[i]);
        ch[i]->lastChange = millis();
        ch[i]->stage      = ch[i]->level ? IND_ON : IND_OFF;
      }
      return (left.pin >= 0 || right.pin >= 0 || drl.pin >= 0);
    }

    void handleIndicator(Channel& c, uint8_t on, uint8_t off, uint8_t clr) {
      if (c.pin < 0) return;
      unsigned long now = millis();
      bool level = digitalRead(c.pin);

      if (level != c.level) {
        c.level = level;
        c.lastChange = now;
        if (level) { c.stage = IND_ON; apply(on); }
        else       { c.stage = IND_OFF; apply(off); }
        return;
      }
      if (level) return;

      if (c.stage == 1 && now - c.lastChange >= transitionDelay) {
        apply(clr);
        c.stage = IND_CLEAR;
        c.lastChange = now;
      }
    }

    void handleDrl() {
      if (drl.pin < 0) return;
      unsigned long now = millis();
      bool level = digitalRead(drl.pin);

      if (level != drl.level) {
        drl.level = level;
        drl.lastChange = now;
        if (level) { apply(pDrlOn); }
        else       { apply(pDrlOff); }
        return;
      }
    }

  public:
    void setup() override {
      enabled  = allocatePins();
      initDone = true;
    }

    void loop() override {
      if (!enabled || strip.isUpdating()) return;

        handleIndicator(left,  pLeftOn,  pLeftOff,  pLeftClear);
        handleIndicator(right, pRightOn, pRightOff, pRightClear);
        handleDrl();
      // switch (index++)
      // {
      // case 0:
      //   handleIndicator(left,  pLeftOn,  pLeftOff,  pLeftClear);
      //   break;
      // case 1: 
      //   handleIndicator(right, pRightOn, pRightOff, pRightClear);
      //   break;
      // case 2:
      //   handleDrl();
      //   index = 0;
      //   break;
      // default:
      //   index = 0;
      //   break;
      // }

    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["left_ind_pin"]      = leftPin;
      top["r_ind_pin"]         = rightPin;
      top["drl_pin"]           = drlPin;
      top["transition_delay"]  = transitionDelay;

      JsonObject p = top.createNestedObject("presets");
      p["l_on"]        = pLeftOn;
      p["l_off"]       = pLeftOff;
      p["l_clear"]     = pLeftClear;
      p["r_on"]        = pRightOn;
      p["r_off"]      = pRightOff;
      p["r_clear"]     = pRightClear;
      p["drl_on"]      = pDrlOn;
      p["drl_off"]     = pDrlOff;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool complete = !top.isNull();

      int8_t oldL = leftPin, oldR = rightPin, oldD = drlPin;

      complete &= getJsonValue(top["left_ind_pin"],     leftPin,         -1);
      complete &= getJsonValue(top["r_ind_pin"],        rightPin,        -1);
      complete &= getJsonValue(top["drl_pin"],          drlPin,          -1);
      complete &= getJsonValue(top["transition_delay"], transitionDelay, 500);

      JsonObject p = top["presets"];
      complete &= !p.isNull();
      complete &= getJsonValue(p["l_on"],        pLeftOn,       0);
      complete &= getJsonValue(p["l_off"],       pLeftOff,      0);
      complete &= getJsonValue(p["l_clear"],     pLeftClear,    0);
      complete &= getJsonValue(p["r_on"],        pRightOn,      0);
      complete &= getJsonValue(p["r_off"],       pRightOff,     0);
      complete &= getJsonValue(p["r_clear"],     pRightClear,   0);
      complete &= getJsonValue(p["drl_on"],      pDrlOn,        0);
      complete &= getJsonValue(p["drl_off"],     pDrlOff,       0);

      // re-allocate pins if they changed after setup() has already run
      if (initDone && (oldL != leftPin || oldR != rightPin || oldD != drlPin)) {
        releasePins();
        enabled = allocatePins();
      }
      return complete;
    }

    // Hints shown in the settings page
    void appendConfigData() override {
      oappend(F("addInfo('Vehicle Lights:transition_delay',1,'ms');"));
      oappend(F("addInfo('Vehicle Lights:presets:l_on',1,'0 = none');"));
    }

    uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

const char VehicleLightsUsermod::_name[] PROGMEM = "Vehicle Lights";

static VehicleLightsUsermod vehicleLights;
REGISTER_USERMOD(vehicleLights);