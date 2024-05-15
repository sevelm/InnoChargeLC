#ifndef LEDEFFECT_HPP
#define LEDEFFECT_HPP

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
void orangeWaveEffect2();
void callLedEffect();

#endif // LEDEFFECT_HPP
