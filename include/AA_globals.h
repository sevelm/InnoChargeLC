#ifndef GLOBALS_H
#define GLOBALS_H
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

// CP-Measurements declaration START
struct cp_measurements{
    float high_voltage;
    float low_voltage;
    float highVoltRaw[10];
    uint16_t highVoltRawCount;
};
typedef struct cp_measurements cp_measurements_t;
extern cp_measurements_t measurements;
extern int cpState;
// CP-Measurements declaration END

// Deklaration des NeoPixel-Strip-Objekts
extern NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip;


#endif 