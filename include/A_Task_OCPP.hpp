#ifndef A_TASK_OCPP_HPP
#define A_TASK_OCPP_HPP

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * A_Task_OCPP
 * -----------
 * Ziel dieses Moduls:
 * - OCPP-Kommunikation in eine eigene Task kapseln.
 * - Klare Schnittstellen bereitstellen, ohne andere Module direkt zu aendern.
 *
 * Aktueller Status:
 * - Bewusst als Scaffold (Startgeruest) aufgebaut.
 * - Enthalten sind Task, Event-Queue, Basiskonfiguration, BootNotification/Heartbeat.
 * - Die eigentliche Anbindung an CP/Meter/RFID erfolgt spaeter ueber Events.
 */

/*
 * OCPP-Eventtypen, die aus anderen Tasks in die OCPP-Task gemeldet werden koennen.
 * Damit bleibt die OCPP-Task entkoppelt von Hardware- oder Protokolldetails.
 */
enum class ocpp_evse_event_type_t : uint8_t {
    None = 0,
    CpStateChanged,
    RfidPresented,
    MeterSample,
    StartRequested,
    StopRequested,
    NetworkUp,
    NetworkDown,
    FaultRaised
};

/*
 * Einfache Event-Nutzlast.
 * "value" kann je nach Eventtyp z. B. Statuscode, Strom, Leistung oder Fehlercode enthalten.
 */
struct ocpp_evse_event_t {
    ocpp_evse_event_type_t type;
    int32_t value;
    uint32_t tsMs;
};

/*
 * Laufzeitwerte zum Debuggen/Monitoring.
 */
struct ocpp_runtime_stats_t {
    bool taskInitialized;
    bool wsConnected;
    bool bootAccepted;
    uint32_t sentFrames;
    uint32_t recvFrames;
    uint32_t droppedEvents;
};

/*
 * Persistente/setzbare OCPP-Verbindungsdaten.
 * Hinweis:
 * - Fuer dieses Geruest wird aktuell "ws://" aktiv unterstuetzt.
 * - "wss://" ist als naechster Schritt vorgesehen.
 */
struct ocpp_server_config_t {
    char wsUrl[192];        // Beispiel: ws://csms.local:9000/ocpp/CP-001
    char chargePointId[64]; // Beispiel: CP-001
    char authToken[96];     // Optional (z. B. Basic/Bearer), aktuell nur abgelegt
};

/*
 * FreeRTOS-Task-Einstiegspunkt.
 * Aufruf spaeter aus main.cpp via xTaskCreatePinnedToCore(...).
 */
void A_Task_OCPP(void* pvParameter);

/*
 * Konfiguration setzen (thread-safe).
 * Diese Funktion startet keine Task und verbindet noch nicht sofort neu;
 * sie aktualisiert nur die Laufzeitkonfiguration fuer den naechsten Verbindungszyklus.
 */
void ocpp_set_server_config(const ocpp_server_config_t* cfg);

/*
 * Event in die OCPP-Queue senden (thread-safe, nicht-blockierend per Timeout).
 * Rueckgabe: true bei Erfolg, false bei voller Queue oder nicht initialisierter Task.
 */
bool ocpp_enqueue_event(const ocpp_evse_event_t& ev, TickType_t timeoutTicks = pdMS_TO_TICKS(10));

/*
 * Laufzeitstatistik lesen (thread-safe).
 * Rueckgabe: true bei Erfolg.
 */
bool ocpp_get_runtime_stats(ocpp_runtime_stats_t* outStats);

#endif // A_TASK_OCPP_HPP
