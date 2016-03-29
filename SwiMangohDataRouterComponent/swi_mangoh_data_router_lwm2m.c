#include "interfaces.h"
#include "legato.h"
#include "swi_mangoh_data_router_db.h"
#include "swi_mangoh_data_router_lwm2m.h"

static void swi_mangoh_data_router_avSvcFieldEventHandler(le_avdata_AssetInstanceRef_t, const char*, void*);

static void swi_mangoh_data_router_avSvcFieldEventHandler(le_avdata_AssetInstanceRef_t assetInstanceRef, const char* fieldName, void* contextPtr)
{
  swi_mangoh_data_router_avsvc_t* avsvc = (swi_mangoh_data_router_avsvc_t*)contextPtr;

  LE_ASSERT(fieldName);

  LE_DEBUG("--> key('%s')", fieldName);
  swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t* fieldEventHandler = le_hashmap_Get(avsvc->fieldEventHndlrs, fieldName);
  if (fieldEventHandler)
  {
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(avsvc->db, fieldName);
    LE_ASSERT(dbItem);
    
    switch(dbItem->data.type)
    {
    case DATAROUTER_BOOLEAN:
      le_avdata_GetBool(avsvc->assetInstanceRef, fieldName, &dbItem->data.bValue);
      LE_DEBUG("--> key('%s'), value('%s')", dbItem->data.key, dbItem->data.bValue ? "true":"false");
      break;

    case DATAROUTER_INTEGER:
      le_avdata_GetInt(avsvc->assetInstanceRef, fieldName, &dbItem->data.iValue);
      LE_DEBUG("--> key('%s'), value(%d)", dbItem->data.key, dbItem->data.iValue);
      break;

    case DATAROUTER_FLOAT:
      le_avdata_GetFloat(avsvc->assetInstanceRef, fieldName, &dbItem->data.fValue);
      LE_DEBUG("--> key('%s'), value(%f)", dbItem->data.key, dbItem->data.fValue);
      break;

    case DATAROUTER_STRING:
      le_avdata_GetString(avsvc->assetInstanceRef, fieldName, dbItem->data.sValue, sizeof(dbItem->data.sValue));
      LE_DEBUG("--> key('%s'), value('%s')", dbItem->data.key, dbItem->data.sValue);
      break;
    }
  }
}

void swi_mangoh_data_router_avSvcSessionStart(const char* asset, swi_mangoh_data_router_avsvc_t* avsvc, swi_mangoh_data_router_db_t* db)
{
  LE_ASSERT(asset);
  LE_ASSERT(avsvc);
  LE_ASSERT(db);

  strcpy(avsvc->asset, asset);
  avsvc->db = db;
  avsvc->assetInstanceRef = le_avdata_Create(avsvc->asset);
  avsvc->fieldEventHndlrs = le_hashmap_Create(SWI_MANGOH_DATA_ROUTER_LWM2M_FIELD_HANDLERS_MAP_NAME, SWI_MANGOH_DATA_ROUTER_LWM2M_FIELD_HANDLERS_MAP_SIZE, 
      le_hashmap_HashString, le_hashmap_EqualsString);
}

void swi_mangoh_data_router_avSvcWrite(swi_mangoh_data_router_dbItem_t* dbItem, swi_mangoh_data_router_avsvc_t* avsvc)
{
  LE_ASSERT(dbItem);
  LE_ASSERT(avsvc);

  switch (dbItem->data.type)
  {
  case DATAROUTER_BOOLEAN:
    LE_DEBUG("AVSVC <-- key('%s'), value('%s'), timestamp(%lu)", dbItem->data.key, dbItem->data.bValue ? "true":"false", dbItem->data.timestamp);
    le_avdata_SetBool(avsvc->assetInstanceRef, dbItem->data.key, dbItem->data.bValue);
    break;

  case DATAROUTER_INTEGER:
    LE_DEBUG("AVSVC <-- key('%s'), value(%d), timestamp(%lu)", dbItem->data.key, dbItem->data.iValue, dbItem->data.timestamp);
    le_avdata_SetInt(avsvc->assetInstanceRef, dbItem->data.key, dbItem->data.iValue);

  case DATAROUTER_FLOAT:
    LE_DEBUG("AVSVC <-- key('%s'), value(%f), timestamp(%lu)", dbItem->data.key, dbItem->data.fValue, dbItem->data.timestamp);
    le_avdata_SetFloat(avsvc->assetInstanceRef, dbItem->data.key, dbItem->data.fValue);

  case DATAROUTER_STRING:
    LE_DEBUG("AVSVC <-- key('%s'), value('%s'), timestamp(%lu)", dbItem->data.key, dbItem->data.sValue, dbItem->data.timestamp);
    le_avdata_SetString(avsvc->assetInstanceRef, dbItem->data.key, dbItem->data.sValue);
    break;
  }

  swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t* fieldEventHandler = le_hashmap_Get(avsvc->fieldEventHndlrs, dbItem->data.key);
  if (!fieldEventHandler)
  {
    fieldEventHandler = calloc(1, sizeof(swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t));
    if (!fieldEventHandler)
    {
      LE_ERROR("ERROR calloc() failed");
      goto cleanup;
    }

    fieldEventHandler->handlerRef = le_avdata_AddFieldEventHandler(avsvc->assetInstanceRef, dbItem->data.key, swi_mangoh_data_router_avSvcFieldEventHandler, avsvc);
    if (!fieldEventHandler->handlerRef)
    {
      LE_ERROR("ERROR le_avdata_AddFieldEventHandler() failed");
      goto cleanup;
    }

    strcpy(fieldEventHandler->key, dbItem->data.key);
    fieldEventHandler->context = avsvc;

    if (le_hashmap_Put(avsvc->fieldEventHndlrs, fieldEventHandler->key, fieldEventHandler))
    {
      LE_ERROR("le_hashmap_Put() failed");
      goto cleanup;
    }
  }

cleanup:
  return;
}

void swi_mangoh_data_router_avSvcSessionEnd(swi_mangoh_data_router_avsvc_t* avsvc)
{
  LE_ASSERT(avsvc);

  if (!le_hashmap_isEmpty(avsvc->fieldEventHndlrs))
  {
    le_hashmap_It_Ref_t iter = le_hashmap_GetIterator(avsvc->fieldEventHndlrs);

    int32_t res = le_hashmap_NextNode(iter);
    while (res == LE_OK)
    {
      swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t* fieldEventUpdateHndlr = (swi_mangoh_data_router_avSvcFieldEventUpdateHndlr_t*)le_hashmap_GetValue(iter);
      if (fieldEventUpdateHndlr)
      {
        le_avdata_RemoveFieldEventHandler(fieldEventUpdateHndlr->handlerRef);
        free(fieldEventUpdateHndlr);
      }

      res = le_hashmap_NextNode(iter);
    }
  }

  le_avdata_Delete(avsvc->assetInstanceRef);
}

