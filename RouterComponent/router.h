/*
 * @file router.h
 *
 * Data router module.
 *
 * This module is the main module for the MangOH data router.
 *
 * <HR>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
#include "legato.h"
#include "interfaces.h"
#include "db.h"
#include "lwm2m.h"
#include "mqtt.h"

#ifndef SWI_MANGOH_DATA_ROUTER_INCLUDE_GUARD
#define SWI_MANGOH_DATA_ROUTER_INCLUDE_GUARD

#define SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_SHORT_NAME "p"
#define SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_LONG_NAME  "protocol"

#define SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_NAME            "DataRouterSessions"
#define SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_SIZE            7

#define SWI_MANGOH_DATA_ROUTER_DATA_HANDLERS_MAP_NAME       "DataRouterDataHndlrs"
#define SWI_MANGOH_DATA_ROUTER_DATA_HANDLERS_MAP_SIZE       7

typedef enum _swi_mangoh_data_router_avProtocol_e
{
  SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE = 0,
  SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT,
  SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M,
} swi_mangoh_data_router_avProtocol_e;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router session
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_session_t
{
  char                                 appId[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN];           ///< Application ID
  dataRouter_Storage_t	       storageType;                                        ///< Data storage
  uint8_t	                       pushAv;                                             ///< Push -> AV flag
  union {
    swi_mangoh_data_router_mqtt_t      mqtt;                                               ///< MQTT protocol -> AV
    swi_mangoh_data_router_avsvc_t     avsvc;                                              ///< Air Vantage Serivce -> AV
  };
} swi_mangoh_data_router_session_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router data update handler
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_dataUpdateHndlr_t
{
  char                                   appId[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN];         ///< Application ID
  char                                   key[SWI_MANGOH_DATA_ROUTER_KEY_MAX_LEN];          ///< Data key
  dataRouter_DataUpdateHandlerFunc_t  handler;                                          ///< Application data update handler function
  void*                                  context;                                          ///< Application context
} swi_mangoh_data_router_dataUpdateHndlr_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router subscriber
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_subscriber_t
{
  char                                  appId[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN];          ///< Application ID
  le_hashmap_Ref_t                      dataUpdateHndlrs;                                   ///< Data update handlers
} swi_mangoh_data_router_subscriber_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router subscribers list element value
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_subscriberLink_t
{
  swi_mangoh_data_router_subscriber_t*  subscriber;                                         ///< Element subscriber data
  le_sls_Link_t                         link;                                               ///< Linked list link to next element
} swi_mangoh_data_router_subscriberLink_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router module
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_t
{
  le_hashmap_Ref_t                      sessions;                                           ///< Data sessions
  swi_mangoh_data_router_db_t           db;                                                 ///< Database module
  swi_mangoh_data_router_avProtocol_e   protocolType;                                       ///< AV push protocol
} swi_mangoh_data_router_t;

void swi_mangoh_data_router_notifySubscribers(const char*, const swi_mangoh_data_router_dbItem_t*);

#endif
