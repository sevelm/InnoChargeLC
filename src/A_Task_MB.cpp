/*********************************************************************
 *  ESP32-S3  ↔︎  SDM-630  +  RFID-Reader  ↔︎  Modbus-TCP
 *  – SDM:   12 Float-Werte (0…23)      alle 2 s
 *  – RFID:  UID-Bytes   (5000…5006)    alle 100 ms
 *  – lib:   emelianov/modbus-esp8266 v4.1.0
 *********************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ModbusRTU.h>
#include <ModbusTCP.h>

#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"
#include "control_pilot.hpp"
#include "AA_globals.h"
#include "ledEffect.hpp"
#include "A_Task_CP.hpp"
#include "control_pilot.hpp"

#ifndef RX_PIN
#  define RX_PIN 45
#  define TX_PIN 35
#  define RTS_PIN 42
#endif

#define BTN_DIN 11            // GPIO-Number (Button by LED)

int16_t mbTcpRegRead09 = 0;

/* ---------- Error-Counter ------------------------------ */
constexpr uint8_t  SDM_ERR_MAX   = 3;      // 3 Fehlversuche  ≈ 6 s
constexpr uint8_t  RFID_ERR_MAX  = 5;      // 5 Fehlversuche  ≈ 0,5 s

/* ---------- Control-Register ------------------------------ */
constexpr uint16_t IO_TCP_BASE = 0;          // Basis 0
constexpr uint8_t  CTRL_CNT    = 10;         // 0–9  (write)
constexpr uint8_t  STAT_CNT    = 10;         // 10–19 (read)
constexpr uint8_t  IO_CNT      = CTRL_CNT + STAT_CNT;   // 20

/* ---------- SDM-Mapping ----------------------------------- */
constexpr uint8_t  SDM_ID = 1;
const struct { uint16_t sdm; uint16_t tcp; } SDM_MAP[] = {
  {0x0000,  0},{0x0002,  2},{0x0004,  4},   // U L1-L3
  {0x0006,  6},{0x0008,  8},{0x000A, 10},   // I L1-L3
  {0x000C, 12},{0x000E, 14},{0x0010, 16},   // P L1-L3
  {0x0034, 18},                             // P total
  {0x0156, 20},{0x0158, 22}                 // E-Imp / E-Exp
};
constexpr uint8_t  SDM_CNT      = sizeof(SDM_MAP)/sizeof(SDM_MAP[0]);
constexpr uint32_t SDM_POLL_MS  = 2000;
constexpr uint16_t SDM_TCP_BASE = 1000;      // 1000 … 1006

/* ---------- RFID-Reader ----------------------------------- */
constexpr uint8_t  RFID_ID      = 2;
constexpr uint16_t RFID_TCP_BASE= 5000;      // 5000 … 5006
constexpr uint32_t RFID_POLL_MS = 100;

/* ---------- Globale Objekte --------------------------------*/
HardwareSerial RS485(2);
ModbusRTU      mbRTU;
ModbusTCP      mbTCP;
static const char* TAG = "Task_MB";

/* ---------- CP-Status-Variablen ----------------------------*/
sdm_data_t sdm;
rfid_data_t rfid; 

static float* const SDM_SLOT[] = {
  &sdm.voltL1,&sdm.voltL2,&sdm.voltL3,
  &sdm.currL1,&sdm.currL2,&sdm.currL3,
  &sdm.pwrL1 ,&sdm.pwrL2 ,&sdm.pwrL3,
  &sdm.pwrTot,&sdm.enrImp,&sdm.enrExp
};

/* ---------- Dummy-Hooks bleiben unverändert ----------------*/
//uint16_t mbRegChargeCurrent=0, mbRegChargePower=1;
//void set_charging_current_mb(float,int){}
//void set_charging_power_mb(float,int){}

/* ---------- Callback – reagiert auf Schreibzugriffe --------*/
uint16_t cbCtrl(TRegister* reg, uint16_t val)
{
    const int16_t sVal = int16_t(val);
    const uint16_t adr = reg->address.address;

    switch (adr) {
        case 4: set_charging_current(float(sVal)); break;
        case 5: set_charging_power  (float(sVal)/100); break;
        case 6: sVal ? turn_off_cp_relay() : turn_on_cp_relay(); break;
        case 9: mbTcpRegRead09 = sVal; break;


        /* weitere Kommandos hier … */
    }
    return val;
}

/* ---------- Callback-Helfer --------------------------------*/
static volatile Modbus::ResultCode lastRc = Modbus::EX_SUCCESS;
static bool trxCB(Modbus::ResultCode rc, uint16_t, void*){ lastRc = rc; return true; }

/* ---------- kleine Helfer ---------------------------------*/
static inline void hregFloat(uint16_t off, const uint16_t* w){
  mbTCP.Hreg(SDM_TCP_BASE + off,     w[0]);
  mbTCP.Hreg(SDM_TCP_BASE + off + 1, w[1]);
}

/* ==================  TASK  ================================ */
void A_Task_MB(void*)
{
    rfid.uidStr     = "00:00:00:00:00:00:00";
    rfid.lastUidStr = "00:00:00:00:00:00:00";

    RS485.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    mbRTU.begin(&RS485, RTS_PIN);  mbRTU.master();

    /* TCP-Register anlegen */
    for (uint16_t i = 0; i < IO_CNT; ++i)  mbTCP.addHreg(IO_TCP_BASE + i);
    mbTCP.onSetHreg(IO_TCP_BASE, cbCtrl, CTRL_CNT);

    for (uint16_t i=0; i<24; ++i) mbTCP.addHreg(SDM_TCP_BASE + i);
    for (uint16_t i=0; i< 7; ++i) mbTCP.addHreg(RFID_TCP_BASE + i);
    mbTCP.server();

    ESP_LOGI(TAG,"Start: SDM %lums, RFID %lums", SDM_POLL_MS, RFID_POLL_MS);

    uint32_t tSDM = 0, tRF = 0;
    TickType_t nextWake = xTaskGetTickCount();
    
    // Init-Error-Counter
    uint8_t sdmErrCnt  = 0;
    uint8_t rfidErrCnt = 0;

    // Button
    pinMode(BTN_DIN, INPUT_PULLUP);   // Pull-Up ON


    while (true)
    {
        mbTCP.task();
        mbRTU.task();

       /* ---------- SDM-Polling & Cleanup --------------------------------- */
    if (sdm.enable)
    {
        bool ok = true;
        const uint16_t FIRST1 = 0x0000, CNT1 = 54;
        uint16_t buf1[CNT1];

      //  ESP_LOGI(TAG, "Reading SDM register block 1 (0x%04X, %u)", FIRST1, CNT1);
        if (!mbRTU.readIreg(SDM_ID, FIRST1, buf1, CNT1, trxCB)) {
            ESP_LOGE(TAG, "readIreg block 1 failed");
            ok = false;
        }
        while (mbRTU.slave()) {
            mbRTU.task(); mbTCP.task(); vTaskDelay(1);
        }
        if (lastRc != Modbus::EX_SUCCESS) {
            ESP_LOGE(TAG, "Modbus error block 1: %d", lastRc);
            ok = false;
        }

        uint16_t buf2[4];
      //  ESP_LOGI(TAG, "Reading SDM register block 2 (0x0156, 4)");
        if (!mbRTU.readIreg(SDM_ID, 0x0156, buf2, 4, trxCB)) {
            ESP_LOGE(TAG, "readIreg block 2 failed");
            ok = false;
        }
        while (mbRTU.slave()) {
            mbRTU.task(); mbTCP.task(); vTaskDelay(1);
        }
        if (lastRc != Modbus::EX_SUCCESS) {
            ESP_LOGE(TAG, "Modbus error block 2: %d", lastRc);
            ok = false;
        }

        if (ok) {
       //     ESP_LOGI(TAG, "Successfully read SDM data");
            for (uint8_t i = 0; i < 10; ++i) {
                const uint16_t* w = &buf1[SDM_MAP[i].sdm - FIRST1];
                union { uint32_t u32; float f; } v{ (uint32_t(w[0])<<16) | w[1] };
                // --- nur bei Power-Werten (Index 6..9) Vorzeichen invertieren ---
                // Index-Layout: 0..2=Volt, 3..5=Curr, 6..8=Power L1..L3, 9=Power Total
                if (i >= 6 && i <= 9) {
                    if (preferences.getBool("emSignEnable", false)) {
                        v.f = -v.f;
                        // in Register auch invertierten Wert schreiben
                        union { uint32_t u32; float f; } out;
                        out.f = v.f;
                        uint16_t ow[2] = { uint16_t(out.u32 >> 16), uint16_t(out.u32 & 0xFFFF) };
                        hregFloat(SDM_MAP[i].tcp, ow);
                    } else {
                        // unverändert in Register
                        hregFloat(SDM_MAP[i].tcp, w);
                    }
                } else {
                    // Nicht-Power: unverändert
                    hregFloat(SDM_MAP[i].tcp, w);
                }
                // immer: lokale Kopie aktualisieren
                *SDM_SLOT[i] = v.f;
            }

            for (uint8_t i = 10; i < SDM_CNT; ++i) {
                const uint16_t* w = &buf2[SDM_MAP[i].sdm - 0x0156];
                union { uint32_t u32; float f; } v{ (uint32_t(w[0])<<16)|w[1] };
           //     ESP_LOGD(TAG, "SDM[%d] @0x%04X = %.2f", i, SDM_MAP[i].sdm, v.f);
                hregFloat(SDM_MAP[i].tcp, w);
                *SDM_SLOT[i] = v.f;
            }

            sdmErrCnt = 0;
            sdm.error = false;
        } else {
            ESP_LOGW(TAG, "Error reading SDM values (%d consecutive errors)", sdmErrCnt + 1);
            if (++sdmErrCnt >= SDM_ERR_MAX) {
                ESP_LOGE(TAG, "SDM reached %d failed cycles → setting error status", SDM_ERR_MAX);
                sdm.error = true;
            }
        }
    }





        /* ---------- RFID-Polling & Cleanup -------------------------------- */
        if (millis() - tRF >= RFID_POLL_MS)         // Zeitfenster erreicht
        {
            tRF = millis();

            /* 1. Wenn Reader aktiv → normal pollen ------------------------ */
            if (rfid.enable)
            {
                bool ok = true;
                uint16_t d[4]{};

                if (!mbRTU.readHreg(RFID_ID, 4, d, 4, trxCB)) ok = false;
                while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
                if (lastRc != Modbus::EX_SUCCESS) ok = false;

                if (ok) {
                    /* UID & Register ablegen ------------------------------ */
                    const uint16_t base = RFID_TCP_BASE;
                    mbTCP.Hreg(base+0, d[0] & 0xFF);
                    mbTCP.Hreg(base+1, d[0] >> 8);
                    mbTCP.Hreg(base+2, d[1] & 0xFF);
                    mbTCP.Hreg(base+3, d[1] >> 8);
                    mbTCP.Hreg(base+4, d[2] & 0xFF);
                    mbTCP.Hreg(base+5, d[2] >> 8);
                    mbTCP.Hreg(base+6, d[3] & 0xFF);

                    rfid.uid[0] = d[0] & 0xFF;  rfid.uid[1] = d[0] >> 8;
                    rfid.uid[2] = d[1] & 0xFF;  rfid.uid[3] = d[1] >> 8;
                    rfid.uid[4] = d[2] & 0xFF;  rfid.uid[5] = d[2] >> 8;
                    rfid.uid[6] = d[3] & 0xFF;

                    char buf[3*7];
                    snprintf(buf, sizeof(buf),
                            "%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                            rfid.uid[0], rfid.uid[1], rfid.uid[2],
                            rfid.uid[3], rfid.uid[4], rfid.uid[5],
                            rfid.uid[6]);
                    rfid.uidStr = String(buf);
                    if (rfid.uidStr != F("00:00:00:00:00:00:00"))
                        rfid.lastUidStr = rfid.uidStr;
                }
                /* Entprellung ------------------------------------------- */
                if (ok) {
                    rfidErrCnt = 0;
                    rfid.error = false;
                } else if (++rfidErrCnt >= RFID_ERR_MAX) {
                    rfid.error = true;          // erst nach 5 Fehlzyklen
                }
            }
            /* 2. Wenn Reader deaktiviert → zyklisch aufräumen ------------ */
            else   /* rfid.enable == false */
            {
                for (uint16_t i = 0; i < 7; ++i) mbTCP.Hreg(RFID_TCP_BASE + i, 0);
                memset(&rfid.uid, 0, sizeof(rfid.uid));
                rfid.uidStr     = F("00:00:00:00:00:00:00");
                rfid.lastUidStr = F("00:00:00:00:00:00:00");
                /* Fehlerstatus zurücksetzen                                */
                rfidErrCnt = 0;
                rfid.error = false;       
            }
        }





        /* ---------- CP-State nach TCP ------------------------- */
        mbTCP.Hreg(IO_TCP_BASE +  0, vCurrentCpState.vehicleConnected);
        mbTCP.Hreg(IO_TCP_BASE +  1, vCurrentCpState.chargingActive);
        mbTCP.Hreg(IO_TCP_BASE + 10, get_cp_state_int());
        mbTCP.Hreg(IO_TCP_BASE + 11, digitalRead(BTN_DIN) == LOW);

        /* ---------- Sleep / Scheduler-Yield ------------------- */
        mbTCP.task();
        vTaskDelay(30 / portTICK_PERIOD_MS); // Adjusted delay
    }
}

