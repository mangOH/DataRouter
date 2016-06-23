/*
 * @file mqtt.h
 *
 * Data router module.
 *
 * This module is the MQTT module for the MangOH data router.
 *
 * <HR>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
#include "legato.h"
#include "interfaces.h"

#include "db.h"

#ifndef SWI_MANGOH_DATA_ROUTER_MQTT_INCLUDE_GUARD
#define SWI_MANGOH_DATA_ROUTER_MQTT_INCLUDE_GUARD

#define SWI_MANGOH_DATA_ROUTER_MQTT_APP_NAME "MQTT"
#define SWI_MANGOH_DATA_ROUTER_MQTT_QUEUED_REQUESTS_MAX_NUM 30
#define SWI_MANGOH_DATA_ROUTER_MQTT_RECONNECT_INTERVAL_SECS 5

#define SWI_MANGOH_DATA_ROUTER_MQTT_URL_LEN 128
#define SWI_MANGOH_DATA_ROUTER_MQTT_PASSWORD_LEN 128

#define SWI_MANGOH_DATA_ROUTER_MQTT_PORT_NUMBER 1883
#define SWI_MANGOH_DATA_ROUTER_MQTT_KEEP_ALIVE 20

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router outstanding requests list element value
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_mqtt_dataLink_t
{
    char key[SWI_MANGOH_DATA_ROUTER_KEY_MAX_LEN];  ///< Key associated with the data
    swi_mangoh_data_router_data_t data;            ///< Element data
    le_sls_Link_t                 link;            ///< Linked list link to next element
} swi_mangoh_data_router_mqtt_dataLink_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router MQTT protocol
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_mqtt_t
{
    char url[SWI_MANGOH_DATA_ROUTER_MQTT_URL_LEN];       ///< Air Vantage URL
    char password[SWI_MANGOH_DATA_ROUTER_MQTT_PASSWORD_LEN]; ///< Air Vantage application model
                                                         ///  password
    mqtt_SessionStateHandlerRef_t sessionStateHdlrRef;   ///< MQTT session state callback function
    mqtt_IncomingMessageHandlerRef_t incomingMsgHdlrRef; ///< MQTT incoming data callback function
    le_timer_Ref_t reconnectTimer;                       ///< Reconnect timer
    le_sls_List_t outstandingRequests;                   ///< Requests waiting to be forwarded
    swi_mangoh_data_router_db_t* db;                     ///< Database module
    bool connected;                                      ///< Air Vantage connected flag
    bool connecting;                                     ///< Air Vantage connecting flag
    bool disconnect;                                     ///< Air Vantage disconnect flag
} swi_mangoh_data_router_mqtt_t;

void swi_mangoh_data_router_mqttReconnect(le_timer_Ref_t);
void swi_mangoh_data_router_mqttSessionStart(
    const char*,
    const char*,
    const char*,
    swi_mangoh_data_router_mqtt_t*,
    swi_mangoh_data_router_db_t*);
bool swi_mangoh_data_router_mqttSessionEnd(swi_mangoh_data_router_mqtt_t*);
void swi_mangoh_data_router_mqttWrite(
    const char* key,
    const swi_mangoh_data_router_dbItem_t*,
    swi_mangoh_data_router_mqtt_t*);

#endif
