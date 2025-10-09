

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include "AA_globals.h"
#include "ledEffect.hpp"

unsigned long prevMillisLED       = 0;    // we use the "millis()" command for time reference and this will output an unsigned long

///////////////// LED Funktionen

int counter1[10];
int counterTag1[10];

int counter2[10];
int counterTag2[10];

int initStateB;
int initStateC;

int ledCase = 0;
int ledNum = 5;

//################################### ??? -> Connected
void stateB () {
   initStateB = 0;  
   initStateC = 0;     
  strip.SetPixelColor(0, RgbColor(255, 195, 0));
  strip.SetPixelColor(1, RgbColor(255, 195, 0));
  strip.SetPixelColor(2, RgbColor(255, 195, 0));
  strip.SetPixelColor(3, RgbColor(255, 195, 0));
  strip.SetPixelColor(4, RgbColor(255, 195, 0));
  strip.SetPixelColor(5, RgbColor(255, 195, 0));
  strip.SetPixelColor(6, RgbColor(255, 195, 0));
  strip.SetPixelColor(7, RgbColor(255, 195, 0));
  strip.Show();   // Send the updated pixel colors to the hardware.
}


//################################### Blue LED -> Standby
void stateA () {
   initStateB = 0;  
   initStateC = 0;     
  strip.SetPixelColor(0, RgbColor(0, 0, 255));
  strip.SetPixelColor(1, RgbColor(0, 0, 255));
  strip.SetPixelColor(2, RgbColor(0, 0, 255));
  strip.SetPixelColor(3, RgbColor(0, 0, 255));
  strip.SetPixelColor(4, RgbColor(0, 0, 255));
  strip.SetPixelColor(5, RgbColor(0, 0, 255));
  strip.SetPixelColor(6, RgbColor(0, 0, 255));
  strip.SetPixelColor(7, RgbColor(0, 0, 255));
  strip.Show();   // Send the updated pixel colors to the hardware.
}

//################################### /* ---------- 0-255 → RGB (HSV-Hue) ---------- */
/* ---------- Modbus-Farbwert 1 … 255 → RGB (Hue-Kreis) ---------- *
 *    1   = Rot         (Start des Farbrads)                       *
 *   43  ≈ Gelb        (Rot → Grün Übergang)                       *
 *   86   = Grün        (120 ° Hue)                                *
 *  128  ≈ Cyan        (Grün → Blau Mitte)                         *
 *  171   = Blau        (240 ° Hue)                                *
 *  213  ≈ Magenta     (Blau → Rot Übergang)                       *
 *  254  ≈ Rot         (Ende des Farbrads)                         *
 *  255   = Weiß        (Sonderfall, reine Vollfarbe)              */
void mbColor(uint8_t val)          // 1-255 aus Modbus
{
    initStateB = 0;
    initStateC = 0;
    /* ---------- Sonderfall 255 → Weiß ------------------- */
    if (val == 255) {
        RgbColor white(255, 255, 255);
        for (uint8_t i = 0; i < 8; ++i)
            strip.SetPixelColor(i, white);
        strip.Show();
        return;                         // fertig
    }
    /* ---------- 1-254 → Hue-Farbrad ---------------------- */
    uint8_t pos = (val - 1) % 254;      // 0-253
    uint8_t r, g, b;

    if (pos < 85) {                     // Rot → Grün
        r = 255 - pos * 3;
        g = pos * 3;
        b = 0;
    }
    else if (pos < 170) {               // Grün → Blau
        pos -= 85;
        r = 0;
        g = 255 - pos * 3;
        b = pos * 3;
    }
    else {                              // Blau → Rot
        pos -= 170;
        r = pos * 3;
        g = 0;
        b = 255 - pos * 3;
    }

    RgbColor c(r, g, b);
    for (uint8_t i = 0; i < 8; ++i)
        strip.SetPixelColor(i, c);
    strip.Show();
}


//################################### UpDown
void stateB_1 () {
  if (ledNum >= 8) {
    ledCase = 1;
  }
  if (ledNum <= 4) {
    ledCase = 0;
  }
  switch (ledCase) {
    case 0:
      for (int y = 0; y <= 255; y += 4) { // For Brightness
        strip.SetPixelColor(ledNum, RgbColor(0, y, y));
        strip.SetPixelColor(7 - ledNum, RgbColor(0, y, y));
        strip.Show();   // Send the updated pixel colors to the hardware.
      //  DELAY(3); // Pause before next pass through loop
      }
      ledNum++;
      break;
    case 1:
      for (int y = 255; y >= 0; y -= 4) { // For Brightness
        strip.SetPixelColor(ledNum, RgbColor(0, y, y));
        strip.SetPixelColor(7 - ledNum, RgbColor(0, y, y));
        strip.Show();   // Send the updated pixel colors to the hardware.
      //  DELAY(3); // Pause before next pass through loop
      }
      ledNum--;
      break;
  }
}

//################################### Wave with Green LED -> Charge Activ
void stateC () {

if (initStateC == 1) {
  switch (counterTag1[0]) {
    case 0: counter1[0]++;
      if (counter1[0] >= 30) {
        counterTag1[0] = 1;
      }
      break;
    case 1: counter1[0]++;
      counter1[1]++;
      if (counter1[1] >= 30) {
        counterTag1[0] = 2;
      }
      break;
    case 2: counter1[0]++;
      counter1[1]++;
      counter1[2]++;
      if (counter1[2] >= 30) {
        counterTag1[0] = 3;
      }
      break;
    case 3: counter1[0]++;
      counter1[1]++;
      counter1[2]++;
      counter1[3]++;      
      if (counter1[3] >= 255) {
        counterTag1[0] = 4;
      }
      break;
    case 4: counter1[3]--;
      if (counter1[3] <= 180) {
        counterTag1[0] = 5;
      }
      break;
    case 5: counter1[2]--;
      counter1[3]--;
      if (counter1[2] <= 180) {
        counterTag1[0] = 6;
      }
      break;
    case 6: counter1[1]--;
      counter1[2]--;
      counter1[3]--;
      if (counter1[1] <= 180) {
        counterTag1[0] = 7;
      }
      break;
    case 7: counter1[0]--;
      counter1[1]--;
      counter1[2]--;
      counter1[3]--;      
      if (counter1[0] <= 0) {
        counterTag1[0] = 0;
      }
      break;     
  }
  if (counter1[0] > 255) {
    counter1[0] = 255;
  }
  if (counter1[1] > 255) {
    counter1[1] = 255;
  }
  if (counter1[2] > 255) {
    counter1[2] = 255;
  }
  if (counter1[3] > 255) {
    counter1[3] = 255;
  }
  if (counter1[0] < 0) {
    counter1[0] = 0;
  }
  if (counter1[1] < 0) {
    counter1[1] = 0;
  }
  if (counter1[2] < 0) {
    counter1[2] = 0;
  }
  if (counter1[3] < 0) {
    counter1[3] = 0;
  }
} else {
  counter1[0] = 255;
  counter1[1] = 255;
  counter1[2] = 255;
  counter1[3] = 255;
  counter1[4] = 255;
  counter1[5] = 255; 
  initStateC = 1;
  counterTag1[0];           
}
  strip.SetPixelColor(4, RgbColor(0, counter1[0], 0));
  strip.SetPixelColor(3, RgbColor(0, counter1[0], 0));
  strip.SetPixelColor(5, RgbColor(0, counter1[1], 0));
  strip.SetPixelColor(2, RgbColor(0, counter1[1], 0));
  strip.SetPixelColor(6, RgbColor(0, counter1[2], 0));
  strip.SetPixelColor(1, RgbColor(0, counter1[2], 0));
  strip.SetPixelColor(7, RgbColor(0, counter1[2], 0));
  strip.SetPixelColor(0, RgbColor(0, counter1[2], 0));  
  strip.Show();   // Send the updated pixel colors to the hardware.
  
}


//################################### ??? -> Error
void stateE () {
  initStateB = 0;  
  initStateC = 0;    
  strip.SetPixelColor(0, RgbColor(255, 0, 0));
  strip.SetPixelColor(1, RgbColor(255, 0, 0));
  strip.SetPixelColor(2, RgbColor(255, 0, 0));
  strip.SetPixelColor(3, RgbColor(255, 0, 0));
  strip.SetPixelColor(4, RgbColor(255, 0, 0));
  strip.SetPixelColor(5, RgbColor(255, 0, 0));
  strip.SetPixelColor(6, RgbColor(255, 0, 0));
  strip.SetPixelColor(7, RgbColor(255, 0, 0));    
  strip.Show();   // Send the updated pixel colors to the hardware.
}

void stateF()
{
    static bool        on   = false;                // merken ob An/​Aus
    static TickType_t  next = 0;                    // nächster Umschalt‑Tick
    const  TickType_t  interval = pdMS_TO_TICKS(250);

    TickType_t now = xTaskGetTickCount();
    if (now >= next) {                              // Zeit zum Umschalten?
        next = now + interval;
        on   = !on;                                 // An ↔ Aus toggeln

        RgbColor col = on ? RgbColor(255, 0, 0)     // Rot
                          : RgbColor(0,   0, 0);    // Aus

        for (uint8_t i = 0; i < 8; ++i) strip.SetPixelColor(i, col);
        strip.Show();
    }
}

//################################### Switch is OFF
void stateSwOff () {
  initStateB = 0;  
  initStateC = 0;    
  strip.SetPixelColor(0, RgbColor(255, 0, 255));
  strip.SetPixelColor(1, RgbColor(255, 0, 255));
  strip.SetPixelColor(2, RgbColor(255, 0, 255));
  strip.SetPixelColor(3, RgbColor(255, 0, 255));
  strip.SetPixelColor(4, RgbColor(255, 0, 255));
  strip.SetPixelColor(5, RgbColor(255, 0, 255));
  strip.SetPixelColor(6, RgbColor(255, 0, 255));
  strip.SetPixelColor(7, RgbColor(255, 0, 255));    
  strip.Show();   // Send the updated pixel colors to the hardware.
}

//################################### PWM is OFF / 100%
void statePwmOff () {
  initStateB = 0;  
  initStateC = 0;    
  strip.SetPixelColor(0, RgbColor(255, 0, 255));   // Magenta
  strip.SetPixelColor(1, RgbColor(255, 0, 255));  
  strip.SetPixelColor(2, RgbColor(255, 0, 255));   
  strip.SetPixelColor(3, RgbColor(255, 0, 255));   
  strip.SetPixelColor(4, RgbColor(255, 0, 255));    
  strip.SetPixelColor(5, RgbColor(255, 0, 255));    
  strip.SetPixelColor(6, RgbColor(255, 0, 255));     
  strip.SetPixelColor(7, RgbColor(255, 0, 255));     
  strip.Show();   // Send the updated pixel colors to the hardware.
}



//################################### true ⇔ DIP „ON“ -> Rescue-Mode
void rescueLedBlink()
{
    static bool   ledOn  = false;
    static uint32_t lastToggle = 0;
    const  uint32_t interval   = 300;           // ms
    
    uint32_t now = millis();
    if (now - lastToggle >= interval) {
        lastToggle = now;
        ledOn      = !ledOn;

        RgbColor col = ledOn ? RgbColor(255, 0, 255)   // Magenta
                             : RgbColor(0);            // Off

        for (uint8_t i = 0; i < 8; ++i) strip.SetPixelColor(i, col);
        strip.Show();
    }
}

void simpleColorChange() {
  static uint8_t hue = 0; // Hue-Wert (Farbwert) von 0 bis 255
  RgbColor color;

  if (counter1[10] >= 5) {
    counter1[10] = 0;
    hue = (hue + 1) % 256; // Ändere den Farbwert für den nächsten Zyklus
  }

  for (int i = 0; i < 8; i++) {
    color = HslColor(hue / 255.0, 1.0, 0.5); // Sättigung und Helligkeit auf 50%
  
  //  strip.SetPixelColor(0, HslColor(hue / 255.0, 1.0, 0.5););
    
    strip.SetPixelColor(i, color); 
  }

  strip.Show();

  counter1[10]++;

}

void knightRiderEffect() {
  static int8_t direction = 1; // Richtung: 1 für vorwärts, -1 für rückwärts
  static int8_t currentPixel = 0;
  static uint8_t counter = 0;
  const uint8_t cycleDuration = 30; // Dauer eines Zyklus in Anzahl der Schritte

  counter1[10]++;
  if (counter1[10] >= cycleDuration) {
    counter1[10] = 0;

    // Alle LEDs ausschalten
    for (int i = 0; i < 8; i++) {
      strip.SetPixelColor(i, RgbColor(255, 195, 0));
    }

    // Aktuelles Pixel einschalten
    strip.SetPixelColor(currentPixel, RgbColor(255, 255, 255));
    strip.Show();

    // Nächstes Pixel ermitteln
    currentPixel += direction;

    // Richtung umkehren, wenn das Ende des Streifens erreicht ist
    if (currentPixel == 8 || currentPixel == -1) {
      direction = -direction;
      currentPixel += 2 * direction; // Nächste Position für den neuen Zyklus vorbereiten
    }
  }
}

void waveEffect() {
  static int8_t direction = 1; // Richtung: 1 für vorwärts, -1 für rückwärts
  static int8_t currentWavePos = 0;
  static uint8_t counter = 0;
  const uint8_t cycleDuration = 5; // Dauer eines Zyklus in Anzahl der Schritte

  counter++;
  if (counter >= cycleDuration) {
    counter = 0;

    // Alle LEDs ausschalten
    for (int i = 0; i < 8; i++) {
      strip.SetPixelColor(i, RgbColor(0, 0, 0)); // Ausschalten
    }

    // LEDs für die Welle einschalten
    int waveLength = 3; // Anzahl der gleichzeitig leuchtenden LEDs für die Welle
    int waveStart = currentWavePos;
    for (int i = 0; i < waveLength; i++) {
      int pixelIndex = waveStart + i * direction;
      if (pixelIndex >= 0 && pixelIndex < 8) {
        int brightness = 255 - abs(i - waveLength / 2) * 50; // Stärke der Welle abhängig von der Position
        strip.SetPixelColor(pixelIndex, RgbColor(brightness, brightness, brightness));
      }
    }
    strip.Show();

    // Nächste Position für die Welle ermitteln
    currentWavePos += direction;

    // Richtung umkehren, wenn das Ende des Streifens erreicht ist
    if (currentWavePos == 8 || currentWavePos == -waveLength) {
      direction = -direction;
      currentWavePos += 2 * direction; // Nächste Position für den neuen Zyklus vorbereiten
    }
  }
}

void orangeWaveEffect2() {
  int init;
  static int8_t direction = 1; // Richtung: 1 für vorwärts, -1 für rückwärts
  static int8_t currentWavePos = 0;
  static uint8_t counter = 0;
  const uint8_t cycleDuration = 30; // Dauer eines Zyklus in Anzahl der Schritte

  if (init == 0) {
    for (int i = 0; i < 8; i++) {
        strip.SetPixelColor(i, RgbColor(255, 195, 0)); // Ausschalten
      }
      init = 1;
    }


  
   counter++; 
  if (counter >= cycleDuration) {
    counter = 0;

    // LEDs für die Welle ausschalten
    int waveLength = 3; // Anzahl der gleichzeitig leuchtenden LEDs für die Welle
    for (int i = 0; i < waveLength; i++) {
      int pixelIndex = currentWavePos + i * direction;
      if (pixelIndex >= 0 && pixelIndex < 8) {
        strip.SetPixelColor(pixelIndex, RgbColor(255, 195, 0)); // Ausschalten
      }
    }

    // Nächste Position für die Welle ermitteln
    currentWavePos += direction;

    // Richtung umkehren, wenn das Ende des Streifens erreicht ist
    if (currentWavePos == 8 || currentWavePos == -waveLength) {
      direction = -direction;
      currentWavePos += 2 * direction; // Nächste Position für den neuen Zyklus vorbereiten
    }

    // LEDs für die Welle einschalten
    for (int i = 0; i < waveLength; i++) {
      int pixelIndex = currentWavePos + i * direction;
      if (pixelIndex >= 0 && pixelIndex < 8) {
        int brightness = 255 - abs(i - waveLength / 2) * 50; // Stärke der Welle abhängig von der Position
        strip.SetPixelColor(pixelIndex, RgbColor(brightness, brightness, brightness));
      }
    }

    strip.Show();
  }
}



// control LED
void callLedEffect()
{
    /* 5-ms-Takt ------------------------------------------------------ */
    if (xTaskGetTickCount() - prevMillisLED < pdMS_TO_TICKS(5))
        return;
    prevMillisLED = xTaskGetTickCount();

    /* ---------- Notfall‑Blinken                     ----------------- */
    if (rescueMode) {                
        rescueLedBlink();
        return;                               // alles Weitere überspringen
    }

    /* ---------- Modbus-Override (1-255 = Farbwert) ----------------- */
    if (mbTcpRegRead09 > 0) {                 // 0  ⇒ kein Override
        mbColor(static_cast<uint8_t>(mbTcpRegRead09));
        return;                               // alles Weitere überspringen
    }

    /* ---------- Normaler CP-Status-Animator ------------------------ */
    switch (currentCpState.state) {
        case StateA_NotConnected:       stateA();            break;
        case StateB_Connected:          stateB();            break;
        case StateC_Charge:             stateC();            break;
        case StateD_VentCharge:         stateC();            break;
        case StateE_Error:              stateE();            break;
        case StateF_Fault:              stateF();            break;
        case StateCustom_CpRelayOff:    stateSwOff();        break;
        case StateCustom_InvalidValue:  mbColor(9);         break;
        case StateCustom_DutyCycle_100: orangeWaveEffect2(); break;
        case StateCustom_DutyCycle_0:   orangeWaveEffect2(); break;
        /* kein default – alle States abgedeckt */
    }
}
