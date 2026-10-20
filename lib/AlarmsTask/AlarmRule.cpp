#include "AlarmRule.h"

bool AlarmRule::update(bool trigger, bool release, uint8_t persistOn, uint8_t persistOff) {
    if (!active) {
        if (trigger) {
            count++;
            if (count >= persistOn) {      // ← tiempo para ACTIVAR
                active = true;
                count = 0;
            }
        } else {
            count = 0;
        }
    } else {
        if (release) {
            count++;
            if (count >= persistOff) {     // ← tiempo para APAGAR
                active = false;
                count = 0;
            }
        } else {
            count = 0;
        }
    }
    return active;
}