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
#include "rfid_db.hpp"
#include "dynamic_power_limit.hpp"
#include <time.h>

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
const struct { uint16_t sdm; uint16_t tcp; } SDM_MAP[] = {
  {0x0000,  0},{0x0002,  2},{0x0004,  4},   // U L1-L3
  {0x0006,  6},{0x0008,  8},{0x000A, 10},   // I L1-L3
  {0x000C, 12},{0x000E, 14},{0x0010, 16},   // P L1-L3
  {0x0034, 18},                             // P total
  {0x0156, 20},{0x0158, 22}                 // E-Imp / E-Exp
};
constexpr uint8_t  SDM_CNT      = sizeof(SDM_MAP)/sizeof(SDM_MAP[0]);
constexpr uint32_t SDM_POLL_MS  = 2000;
const struct { uint16_t reg; uint16_t tcp; } YT_DTS353F2_MAP[] = {
  {0x000E,  0},{0x0010,  2},{0x0012,  4},   // U L1-L3
  {0x0016,  6},{0x0018,  8},{0x001A, 10},   // I L1-L3
  {0x001E, 12},{0x0020, 14},{0x0022, 16},   // P L1-L3
  {0x001C, 18},                             // P total
  {0x0110, 20},{0x0108, 22}                 // E-Imp / E-Exp
};
constexpr uint8_t YT_DTS353F2_CNT = sizeof(YT_DTS353F2_MAP)/sizeof(YT_DTS353F2_MAP[0]);
constexpr uint16_t SDM_TCP_BASE = 1000;      // 1000 … 1006

/* ---------- RFID-Reader ----------------------------------- */
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
rfid_auth_t rfidAuth;
charge_auth_session_t chargeAuthSession;

static bool lastRfidBuzzerWritten = false;
static uint8_t lastRfidLedWritten = 255;
static String lastRfidReaderIoTag = "";
static uint32_t rfidReaderBuzzerUntilMillis = 0;
static uint32_t lastRfidBuzzerOffWriteMillis = 0;
constexpr uint32_t RFID_READER_BUZZER_MS = 1000;
constexpr uint32_t RFID_READER_BUZZER_OFF_REFRESH_MS = 1000;

static bool isEmptyRfidTag(const String& idTag)
{
    return idTag == F("00:00:00:00:00:00:00") || idTag.length() == 0;
}

static bool isChargeStateActive()
{
    return currentCpState.state == StateC_Charge ||
           currentCpState.state == StateD_VentCharge;
}

static uint8_t currentRfidReaderLed()
{
    if (!rfidAuth.required) return rfid.led;
    if (!isEmptyRfidTag(rfid.uidStr) && !chargeAuthSession.authorized) return 2;
    if (chargeAuthSession.authorized) return 1;
    return 0;
}

static void grantChargeAuthSession(const String& idTag, const String& userName, uint16_t maxChargeMinutes)
{
    chargeAuthSession.authorized = true;
    chargeAuthSession.vehicleWasConnected = false;
    chargeAuthSession.authorizationGrantedMillis = millis();
    chargeAuthSession.lastChargeActiveMillis = 0;
    chargeAuthSession.authorizationGrantedTime = time(nullptr);
    chargeAuthSession.lastChargeActiveTime = 0;
    chargeAuthSession.idTag = idTag;
    chargeAuthSession.userName = userName;
    chargeAuthSession.maxChargeMinutes = maxChargeMinutes;
    apply_charging_authorization();
}

static void clearChargeAuthSessionFromRfid()
{
    chargeAuthSession.authorized = false;
    chargeAuthSession.vehicleWasConnected = false;
    chargeAuthSession.authorizationGrantedMillis = 0;
    chargeAuthSession.lastChargeActiveMillis = 0;
    chargeAuthSession.authorizationGrantedTime = 0;
    chargeAuthSession.lastChargeActiveTime = 0;
    chargeAuthSession.idTag = "";
    chargeAuthSession.userName = "";
    chargeAuthSession.maxChargeMinutes = 0;
    apply_charging_authorization();
}

static void updateRfidAuthorizationFromCurrentTag()
{
    static String lastGrantedTag = "";

    rfid_user_t currentUser;
    rfidAuth.authorized = rfid_db_is_authorized(rfid.uidStr, &currentUser);

    if (isEmptyRfidTag(rfid.uidStr)) {
        lastGrantedTag = "";
    } else if (rfidAuth.authorized && !chargeAuthSession.authorized && lastGrantedTag != rfid.uidStr) {
        grantChargeAuthSession(rfid.uidStr, currentUser.name, currentUser.maxChargeMinutes);
        lastGrantedTag = rfid.uidStr;
    } else if (!rfidAuth.authorized && rfidAuth.required && !isEmptyRfidTag(rfid.uidStr)) {
        if (chargeAuthSession.authorized &&
            !isChargeStateActive() &&
            rfid.uidStr != chargeAuthSession.idTag) {
            clearChargeAuthSessionFromRfid();
            lastGrantedTag = "";
        }
    }
}

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

static uint32_t currentRs485Config = SERIAL_8N1;

static void setRs485Config(uint32_t config)
{
    if (currentRs485Config == config) return;

    while (mbRTU.slave()) {
        mbRTU.task();
        mbTCP.task();
        vTaskDelay(1);
    }

    RS485.flush();
    RS485.end();
    delay(5);
    RS485.begin(9600, config, RX_PIN, TX_PIN);
    while (RS485.available() > 0) RS485.read();
    currentRs485Config = config;
}

static bool waitModbusDone()
{
    while (mbRTU.slave()) {
        mbRTU.task();
        mbTCP.task();
        vTaskDelay(1);
    }
    return lastRc == Modbus::EX_SUCCESS;
}

static void storeMeterFloat(uint8_t i, uint16_t tcpOff, const uint16_t* w, float scale = 1.0f, bool invertPowerSign = false)
{
    union { uint32_t u32; float f; } v{ (uint32_t(w[0]) << 16) | w[1] };
    v.f *= scale;

    if (i >= 6 && i <= 9 && invertPowerSign) {
        v.f = -v.f;
    }

    if (scale != 1.0f || (i >= 6 && i <= 9 && invertPowerSign)) {
        union { uint32_t u32; float f; } out;
        out.f = v.f;
        uint16_t ow[2] = { uint16_t(out.u32 >> 16), uint16_t(out.u32 & 0xFFFF) };
        hregFloat(tcpOff, ow);
    } else {
        hregFloat(tcpOff, w);
    }

    *SDM_SLOT[i] = v.f;
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
	        if (sdm.type == EnergyMeter_YtDts353F2) {
	            setRs485Config(SERIAL_8E1);
	            for (uint8_t i = 0; i < YT_DTS353F2_CNT; ++i) {
	                uint16_t w[2]{};
	                lastRc = Modbus::EX_SUCCESS;
	                if (!mbRTU.readHreg(sdm.modbusId, YT_DTS353F2_MAP[i].reg, w, 2, trxCB)) {
	                    ESP_LOGE(TAG, "readHreg YT-DTS353F-2 0x%04X failed", YT_DTS353F2_MAP[i].reg);
	                    ok = false;
	                    break;
	                }
	                if (!waitModbusDone()) {
	                    ESP_LOGE(TAG, "Modbus error YT-DTS353F-2 0x%04X: %d", YT_DTS353F2_MAP[i].reg, lastRc);
	                    ok = false;
	                    break;
	                }
                storeMeterFloat(i, YT_DTS353F2_MAP[i].tcp, w, (i >= 6 && i <= 9) ? 1000.0f : 1.0f,
                                !preferences.getBool("emSignEnable", false));
	            }
	        } else {
	            setRs485Config(SERIAL_8N1);
	        const uint16_t FIRST1 = 0x0000, CNT1 = 54;
        uint16_t buf1[CNT1];

	      //  ESP_LOGI(TAG, "Reading SDM register block 1 (0x%04X, %u)", FIRST1, CNT1);
	        lastRc = Modbus::EX_SUCCESS;
        if (!mbRTU.readIreg(sdm.modbusId, FIRST1, buf1, CNT1, trxCB)) {
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
	        lastRc = Modbus::EX_SUCCESS;
		        if (!mbRTU.readIreg(sdm.modbusId, 0x0156, buf2, 4, trxCB)) {
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

	        }

	        }

	        if (ok) {
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
	
			                setRs485Config(SERIAL_8N1);
			                const uint32_t now = millis();
			                const bool desiredRfidBuzzer = rfid.buzzer || now < rfidReaderBuzzerUntilMillis;
			                const bool refreshRfidBuzzerOff = !desiredRfidBuzzer && now - lastRfidBuzzerOffWriteMillis >= RFID_READER_BUZZER_OFF_REFRESH_MS;
			                const uint8_t desiredRfidLed = currentRfidReaderLed();

			                if (desiredRfidBuzzer != lastRfidBuzzerWritten || refreshRfidBuzzerOff) {
			                    lastRc = Modbus::EX_SUCCESS;
				                    if (!mbRTU.writeCoil(rfid.modbusId, 1, desiredRfidBuzzer, trxCB)) ok = false;
			                    while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
			                    if (lastRc != Modbus::EX_SUCCESS) ok = false;
			                    if (ok) {
			                        lastRfidBuzzerWritten = desiredRfidBuzzer;
			                        if (!desiredRfidBuzzer) lastRfidBuzzerOffWriteMillis = now;
			                    }
			                }

		                if (desiredRfidLed != lastRfidLedWritten) {
		                    if (desiredRfidLed == 1) {
		                        lastRc = Modbus::EX_SUCCESS;
			                        if (!mbRTU.writeCoil(rfid.modbusId, 2, true, trxCB)) ok = false;
		                        while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
		                        if (lastRc != Modbus::EX_SUCCESS) ok = false;
		                    } else if (desiredRfidLed == 2) {
		                        lastRc = Modbus::EX_SUCCESS;
			                        if (!mbRTU.writeCoil(rfid.modbusId, 3, true, trxCB)) ok = false;
		                        while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
	                        if (lastRc != Modbus::EX_SUCCESS) ok = false;
	                    } else {
	                        lastRc = Modbus::EX_SUCCESS;
		                        if (!mbRTU.writeCoil(rfid.modbusId, 2, false, trxCB)) ok = false;
	                        while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
	                        if (lastRc != Modbus::EX_SUCCESS) ok = false;

	                        if (ok) {
	                            lastRc = Modbus::EX_SUCCESS;
			                        if (!mbRTU.writeCoil(rfid.modbusId, 3, false, trxCB)) ok = false;
	                            while (mbRTU.slave()) { mbRTU.task(); vTaskDelay(1); }
	                            if (lastRc != Modbus::EX_SUCCESS) ok = false;
		                        }
		                    }

		                    if (ok) lastRfidLedWritten = desiredRfidLed;
		                }

		                lastRc = Modbus::EX_SUCCESS;
			                if (!mbRTU.readHreg(rfid.modbusId, 4, d, 4, trxCB)) ok = false;
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
	                    if (rfid.uidStr != F("00:00:00:00:00:00:00")) {
	                        rfid.lastUidStr = rfid.uidStr;
	                        if (rfid.uidStr != lastRfidReaderIoTag) {
	                            rfidReaderBuzzerUntilMillis = millis() + RFID_READER_BUZZER_MS;
	                            lastRfidReaderIoTag = rfid.uidStr;
	                        }
	                    } else {
	                        lastRfidReaderIoTag = "";
	                    }
	                    updateRfidAuthorizationFromCurrentTag();
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
                rfidAuth.authorized = false;
                /* Fehlerstatus zurücksetzen                                */
                rfidErrCnt = 0;
                rfid.error = false;       
            }
        }





        /* ---------- CP-State nach TCP ------------------------- */
        mbTCP.Hreg(IO_TCP_BASE +  0, vCurrentCpState.vehicleConnected);
        mbTCP.Hreg(IO_TCP_BASE +  1, vCurrentCpState.chargingActive);
        mbTCP.Hreg(IO_TCP_BASE +  2, vCurrentCpState.threePhaseActive);
        mbTCP.Hreg(IO_TCP_BASE + 10, get_cp_state_int());
        mbTCP.Hreg(IO_TCP_BASE + 11, digitalRead(BTN_DIN) == LOW);

        /* ---------- Sleep / Scheduler-Yield ------------------- */
        mbTCP.task();
        vTaskDelay(30 / portTICK_PERIOD_MS); // Adjusted delay

        //vTaskDelay(pdMS_TO_TICKS(10));
    }
}

