/*
 * @file lwm2m.h
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

#ifndef SWI_MANGOH_DATA_ROUTER_LWM2M_INCLUDE_GUARD
#define SWI_MANGOH_DATA_ROUTER_LWM2M_INCLUDE_GUARD

#define SWI_MANGOH_DATA_ROUTER_LWM2M_APP_NAME                     "LWM2M"
#define SWI_MANGOH_DATA_ROUTER_LWM2M_ASSET_LEN		          128

#define SWI_MANGOH_DATA_ROUTER_LWM2M_FIELD_HANDLERS_MAP_NAME      "LWM2MFieldHndlrs"
#define SWI_MANGOH_DATA_ROUTER_LWM2M_FIELD_HANDLERS_MAP_SIZE      63

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router AV Service field event handler
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t
{
  char                                   key[SWI_MANGOH_DATA_ROUTER_KEY_MAX_LEN];          ///< Data key
  le_avdata_FieldEventHandlerRef_t       handlerRef;                                       ///< Field event handler reference
  void*                                  context;                                          ///< Application context
} swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t;

//------------------------------------------------------------------------------------------------------------------
/**
 * Data Router AV Data protocol
 */
//------------------------------------------------------------------------------------------------------------------
typedef struct _swi_mangoh_data_router_avsvc_t
{
  char                                  asset[SWI_MANGOH_DATA_ROUTER_LWM2M_ASSET_LEN];     ///< Air Vantage asset
  le_avdata_AssetInstanceRef_t          assetInstanceRef;                                  ///< Air Vantage instance reference
  le_hashmap_Ref_t                      fieldEventHndlrs;                                  ///< Air Vantage field handlers
  swi_mangoh_data_router_db_t*          db;                                                ///< Database module
} swi_mangoh_data_router_avsvc_t;

void swi_mangoh_data_router_avSvcSessionStart(const char* asset, swi_mangoh_data_router_avsvc_t*, swi_mangoh_data_router_db_t*);
void swi_mangoh_data_router_avSvcSessionEnd(swi_mangoh_data_router_avsvc_t*);
void swi_mangoh_data_router_avSvcWrite(swi_mangoh_data_router_dbItem_t* dbItem, swi_mangoh_data_router_avsvc_t*);

#endif
