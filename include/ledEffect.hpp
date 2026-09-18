#ifndef LEDEFFECT_HPP
#define LEDEFFECT_HPP

#include <stdint.h>

enum class LedVisualAnimation : uint8_t { Solid, Wave, Charge, Blink };

struct LedVisualStatus {
    const char* label;
    uint32_t color;
    uint32_t waveColor;
    LedVisualAnimation animation;
    uint16_t periodMs;
};

// Snapshot of the effect actually selected by the LED task, including overrides.
LedVisualStatus getLedVisualStatus();

extern unsigned long prevMillisLED;
extern int counter1[10];
extern int counterTag1[10];
extern int counter2[10];
extern int counterTag2[10];
extern int initStateB;
extern int initStateC;
extern int ledCase;
extern int ledNum;

void stateB();
void stateA();
void stateB_1();
void stateC();
void stateE();
void stateF();
void stateSwOff();
void statePwmOff();
void simpleColorChange();
void knightRiderEffect();
void waveEffect();
void callLedEffect();
void A_Task_LED(void* pvParameter);
void mbColor();
void rescueLedBlink();


#endif // LEDEFFECT_HPP
