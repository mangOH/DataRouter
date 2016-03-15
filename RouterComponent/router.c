#include "interfaces.h"
#include "legato.h"
#include "router.h"

static void swi_mangoh_data_router_SigTermEventHandler(int);
static le_result_t swi_mangoh_data_router_getClientAppId(char[], size_t);
static void swi_mangoh_data_router_selectAvProtocol(const char*);

static swi_mangoh_data_router_t dataRouter;

static void swi_mangoh_data_router_SigTermEventHandler(int sigNum)
{
  swi_mangoh_data_router_db_destroy(&dataRouter.db);
}

static le_result_t swi_mangoh_data_router_getClientAppId(char appName[], size_t len)
{
  pid_t processId = {0};
  le_result_t res = LE_OK;

  res = le_msg_GetClientProcessId(dataRouter_GetClientSessionRef(), &processId);
  if (res != LE_OK)
  {
    LE_ERROR("ERROR le_msg_GetClientProcessId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("process(%u), len(%u)", processId, len);
  res = le_appInfo_GetName(processId, appName, len);	
  if (res != LE_OK)
  {
    LE_ERROR("ERROR le_appInfo_GetName() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("app name('%s')", appName);

cleanup:
  return res;
}

static void swi_mangoh_data_router_selectAvProtocol(const char* value)
{
  LE_ASSERT(value);
  LE_DEBUG("AV protocol value('%s')", value);

  if (!strcmp(value, SWI_MANGOH_DATA_ROUTER_MQTT_APP_NAME))
  {
    LE_DEBUG("AV protocol MQTT");
    dataRouter.protocolType = SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT;
  }
  else if (!strcmp(value, SWI_MANGOH_DATA_ROUTER_LWM2M_APP_NAME))
  {
    LE_DEBUG("AV protocol LWM2M");
    dataRouter.protocolType = SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M;
  }
}

void swi_mangoh_data_router_notifySubscribers(const char* appId, const swi_mangoh_data_router_dbItem_t* dbItem)
{
  LE_ASSERT(appId);
  LE_ASSERT(dbItem);

  LE_DEBUG("application ID('%s')", appId);

  le_sls_Link_t* linkPtr = le_sls_Peek(&dbItem->subscribers);
  while (linkPtr)
  {
    swi_mangoh_data_router_subscriberLink_t* subscriberElem = CONTAINER_OF(linkPtr, swi_mangoh_data_router_subscriberLink_t, link);

    LE_DEBUG("subscriber('%s')", subscriberElem->subscriber->appId);
    if (strcmp(subscriberElem->subscriber->appId, appId))
    {
      swi_mangoh_data_router_dataUpdateHndlr_t* dataUpdateHndlr = le_hashmap_Get(subscriberElem->subscriber->dataUpdateHndlrs, dbItem->data.key);
      if (dataUpdateHndlr)
      {
        LE_DEBUG("subscriber('%s') data update handler key('%s')", subscriberElem->subscriber->appId, dbItem->data.key);
        dataUpdateHndlr->handler(dbItem->data.type, dbItem->data.key, dataUpdateHndlr->context);
      }
      else
      {
        LE_DEBUG("subscriber('%s') NO data update handler key('%s')", subscriberElem->subscriber->appId, dbItem->data.key);
      }
    }

    linkPtr = le_sls_PeekNext(&dbItem->subscribers, linkPtr);
  }
}

void dataRouter_SessionStart(const char* urlAsset, const char* password, uint8_t pushAv, dataRouter_Storage_t storage)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(urlAsset);
  LE_ASSERT(password);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (!session)
  {
    session = calloc(1, sizeof(swi_mangoh_data_router_session_t));
    if (!session)
    {
      LE_ERROR("ERROR calloc() failed");
      goto cleanup;
    }

    strcpy(session->appId, appName);
    session->pushAv = pushAv;
    session->storageType = storage;

    if (session->pushAv)
    {
      switch (dataRouter.protocolType)
      {
      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
        swi_mangoh_data_router_mqttSessionStart(appName, urlAsset, password, &session->mqtt, &dataRouter.db);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
        swi_mangoh_data_router_avSvcSessionStart(urlAsset, &session->avsvc, &dataRouter.db);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
        break;

      default:
        LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
        break;
      }
    }

    if (le_hashmap_Put(dataRouter.sessions, session->appId, session))
    {
      LE_ERROR("le_hashmap_Put() failed");
      goto cleanup;
    }

    LE_DEBUG("added session('%s')", appName);
  }
  else
  {
    LE_WARN("session('%s') exists", appName);
  }

cleanup:
  return;
}

void dataRouter_SessionEnd(void)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session && session->pushAv)
  {
    switch (dataRouter.protocolType)
    {
    case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
      if (swi_mangoh_data_router_mqttSessionEnd(&session->mqtt))
      {
        if (!le_hashmap_Remove(dataRouter.sessions, appName))
        {
          LE_ERROR("ERROR le_hashmap_Remove() failed");
          free(session);
          goto cleanup;
        }

        free(session);
      }

      break;

    case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
      swi_mangoh_data_router_avSvcSessionEnd(&session->avsvc);

      if (!le_hashmap_Remove(dataRouter.sessions, appName))
      {
        LE_ERROR("ERROR le_hashmap_Remove() failed");
        free(session);
        goto cleanup;
      }

      free(session);
      break;

    case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
      break;

    default:
      LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
      break;
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_WriteBoolean(const char* key, bool value, uint32_t timestamp)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    LE_DEBUG("app('%s') --> key('%s'), value(%d), timestamp(%u)", appName, key, value, timestamp);

    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (!dbItem)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
        goto cleanup;
      }
    }

    swi_mangoh_data_router_db_setStorageType(dbItem, session->storageType);
    swi_mangoh_data_router_db_setDataType(dbItem, DATAROUTER_BOOLEAN);
    swi_mangoh_data_router_db_setBooleanValue(dbItem, value);
    swi_mangoh_data_router_db_setTimestamp(dbItem, timestamp);

    if (session->pushAv)
    {
      switch (dataRouter.protocolType)
      {
        case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
        swi_mangoh_data_router_mqttWrite(dbItem, &session->mqtt);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
        swi_mangoh_data_router_avSvcWrite(dbItem, &session->avsvc);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
        break;

      default:
        LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
        break;
      }
    }

    swi_mangoh_data_router_notifySubscribers(appName, dbItem);
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_WriteInteger(const char* key, int32_t value, uint32_t timestamp)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    LE_DEBUG("app('%s') --> key('%s'), value(%d), timestamp(%u)", appName, key, value, timestamp);

    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (!dbItem)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
        goto cleanup;
      }
    }

    swi_mangoh_data_router_db_setStorageType(dbItem, session->storageType);
    swi_mangoh_data_router_db_setDataType(dbItem, DATAROUTER_INTEGER);
    swi_mangoh_data_router_db_setIntegerValue(dbItem, value);
    swi_mangoh_data_router_db_setTimestamp(dbItem, timestamp);

    if (session->pushAv)
    {
      switch (dataRouter.protocolType)
      {
      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
        swi_mangoh_data_router_mqttWrite(dbItem, &session->mqtt);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
        swi_mangoh_data_router_avSvcWrite(dbItem, &session->avsvc);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
        break;

      default:
        LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
        break;
      }
    }

    swi_mangoh_data_router_notifySubscribers(appName, dbItem);
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_WriteFloat(const char* key, float value, uint32_t timestamp)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    LE_DEBUG("app('%s') --> key('%s'), value(%f), timestamp(%u)", appName, key, value, timestamp);

    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (!dbItem)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
        goto cleanup;
      }
    }

    swi_mangoh_data_router_db_setStorageType(dbItem, session->storageType);
    swi_mangoh_data_router_db_setDataType(dbItem, DATAROUTER_FLOAT);
    swi_mangoh_data_router_db_setFloatValue(dbItem, value);
    swi_mangoh_data_router_db_setTimestamp(dbItem, timestamp);

    if (session->pushAv)
    {
      switch (dataRouter.protocolType)
      {
      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
        swi_mangoh_data_router_mqttWrite(dbItem, &session->mqtt);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
        swi_mangoh_data_router_avSvcWrite(dbItem, &session->avsvc);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
        break;

      default:
        LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
        break;
      }
    }

    swi_mangoh_data_router_notifySubscribers(appName, dbItem);
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_WriteString(const char* key, const char* value, uint32_t timestamp)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);
  LE_ASSERT(value);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    LE_DEBUG("app('%s') --> key('%s'), value('%s'), timestamp(%u)", appName, key, value, timestamp);

    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (!dbItem)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
        goto cleanup;
      }
    }

    swi_mangoh_data_router_db_setStorageType(dbItem, session->storageType);
    swi_mangoh_data_router_db_setDataType(dbItem, DATAROUTER_STRING);
    swi_mangoh_data_router_db_setStringValue(dbItem, value);
    swi_mangoh_data_router_db_setTimestamp(dbItem, timestamp);

    if (session->pushAv)
    {
      switch (dataRouter.protocolType)
      {
      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
        swi_mangoh_data_router_mqttWrite(dbItem, &session->mqtt);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
        swi_mangoh_data_router_avSvcWrite(dbItem, &session->avsvc);
        break;

      case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
        break;

      default:
        LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
        break;
      }
    }

    swi_mangoh_data_router_notifySubscribers(appName, dbItem);
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_ReadBoolean(const char* key, bool* valuePtr, uint32_t* timestampPtr)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (dbItem)
    {
      if (dbItem->data.type == DATAROUTER_BOOLEAN)
      {
        memcpy(valuePtr, &dbItem->data.bValue, sizeof(dbItem->data.bValue));
        memcpy(timestampPtr, &dbItem->data.timestamp, sizeof(dbItem->data.timestamp));
        LE_DEBUG("app('%s') <-- key('%s'), value(%d), timestamp(%u)", appName, key, *valuePtr, *timestampPtr);
      }
      else
      {
        LE_WARN("key('%s') not BOOLEAN type", key);
      }
    }
    else
    {
      LE_WARN("key('%s') not found", key);
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_ReadInteger(const char* key, int32_t* valuePtr, uint32_t* timestampPtr)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (dbItem)
    {
      if (dbItem->data.type == DATAROUTER_INTEGER)
      {
        memcpy(valuePtr, &dbItem->data.iValue, sizeof(dbItem->data.iValue));
        memcpy(timestampPtr, &dbItem->data.timestamp, sizeof(dbItem->data.timestamp));
        LE_DEBUG("app('%s') <-- key('%s'), value(%d), timestamp(%u)", appName, key, *valuePtr, *timestampPtr);
      }
      else
      {
        LE_WARN("key('%s') not INTEGER type", key);
      }
    }
    else
    {
      LE_WARN("key('%s') not found", key);
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_ReadFloat(const char* key, float* valuePtr, uint32_t* timestampPtr)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (dbItem)
    {
      if (dbItem->data.type == DATAROUTER_FLOAT)
      {
        memcpy(valuePtr, &dbItem->data.fValue, sizeof(dbItem->data.fValue));
        memcpy(timestampPtr, &dbItem->data.timestamp, sizeof(dbItem->data.timestamp));
        LE_DEBUG("app('%s') <-- key('%s'), value(%f), timestamp(%u)", appName, key, *valuePtr, *timestampPtr);
      }
      else
      {
        LE_WARN("key('%s') not FLOAT type", key);
      }
    }
    else
    {
      LE_WARN("key('%s') not found", key);
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

void dataRouter_ReadString(const char* key, char* valuePtr, size_t numValues, uint32_t* timestampPtr)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  LE_ASSERT(key);

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (dbItem)
    {
      if (dbItem->data.type == DATAROUTER_STRING)
      {
        memset(valuePtr, 0, numValues);
        strcpy(valuePtr, dbItem->data.sValue);

        memcpy(timestampPtr, &dbItem->data.timestamp, sizeof(dbItem->data.timestamp));
        LE_DEBUG("app('%s') <-- key('%s'), value('%s'), timestamp(%u)", appName, key, valuePtr, *timestampPtr);
      }
      else
      {
        LE_WARN("key('%s') not STRING type", key);
      }
    }
    else
    {
      LE_WARN("key('%s') not found", key);
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return;
}

dataRouter_DataUpdateHandlerRef_t dataRouter_AddDataUpdateHandler(const char* key, dataRouter_DataUpdateHandlerFunc_t handlerPtr, void* contextPtr)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  swi_mangoh_data_router_subscriber_t* subscriber = NULL;
  swi_mangoh_data_router_dataUpdateHndlr_t* dataUpdateHndlr = NULL;
  le_result_t res = LE_OK;

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    LE_DEBUG("appId('%s') register handler('%s')", appName, key);
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
    if (!dbItem)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
        goto cleanup;
      }
    }

    le_sls_Link_t* linkPtr = le_sls_Peek(&dbItem->subscribers);
    while (linkPtr)
    {
      swi_mangoh_data_router_subscriberLink_t* subscriberElem = CONTAINER_OF(linkPtr, swi_mangoh_data_router_subscriberLink_t, link);

      if (!strcmp(subscriberElem->subscriber->appId, appName))
      {
        subscriber = subscriberElem->subscriber;
        break;
      }

      linkPtr = le_sls_PeekNext(&dbItem->subscribers, linkPtr);
    }

    if (!linkPtr)
    {
      subscriber = calloc(1, sizeof(swi_mangoh_data_router_subscriber_t));
      if (!subscriber)
      {
        LE_ERROR("ERROR calloc() failed");
        goto cleanup;
      }

      subscriber->dataUpdateHndlrs = le_hashmap_Create(SWI_MANGOH_DATA_ROUTER_DATA_HANDLERS_MAP_NAME, SWI_MANGOH_DATA_ROUTER_DATA_HANDLERS_MAP_SIZE, le_hashmap_HashString, le_hashmap_EqualsString);
      strcpy(subscriber->appId, appName);

      swi_mangoh_data_router_subscriberLink_t* subscriberElem = calloc(1, sizeof(swi_mangoh_data_router_subscriberLink_t));
      if (!subscriberElem)
      {
        LE_ERROR("ERROR calloc() failed");
        free(subscriber);
        goto cleanup;
      }

      LE_DEBUG("key('%s') add subscriber('%s')", key, appName);
      subscriberElem->link = LE_SLS_LINK_INIT;
      subscriberElem->subscriber = subscriber;
      le_sls_Queue(&dbItem->subscribers, &subscriberElem->link);
    }

    dataUpdateHndlr = le_hashmap_Get(subscriber->dataUpdateHndlrs, key);
    if (!dataUpdateHndlr)
    {
      dataUpdateHndlr = calloc(1, sizeof(swi_mangoh_data_router_dataUpdateHndlr_t));
      if (!dataUpdateHndlr)
      {
        LE_ERROR("ERROR calloc() failed");
        goto cleanup;
      }

      strcpy(dataUpdateHndlr->appId, appName);
      strcpy(dataUpdateHndlr->key, key);
      dataUpdateHndlr->handler = handlerPtr;
      dataUpdateHndlr->context = contextPtr;

      if (le_hashmap_Put(subscriber->dataUpdateHndlrs, dataUpdateHndlr->key, dataUpdateHndlr))
      {
        LE_ERROR("ERROR le_hashmap_Put() failed");
        free(dataUpdateHndlr);
        goto cleanup;
      }
    }
    else
    {
      LE_WARN("data update handler('%s') exists", key);
    }
  }
  else
  {
    LE_WARN("session('%s') not found", appName);
  }

cleanup:
  return (dataRouter_DataUpdateHandlerRef_t)(dataUpdateHndlr);
}

void dataRouter_RemoveDataUpdateHandler(dataRouter_DataUpdateHandlerRef_t addHandlerRef)
{
  char appName[SWI_MANGOH_DATA_ROUTER_APP_ID_LEN] = {0};
  le_result_t res = LE_OK;

  res = swi_mangoh_data_router_getClientAppId(appName, sizeof(appName));
  if (res != LE_OK)
  {
    LE_ERROR("ERROR swi_mangoh_data_router_getClientAppId() failed(%d)", res);
    goto cleanup;
  }

  LE_DEBUG("lookup session('%s')", appName);
  swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, appName);
  if (session)
  {
    swi_mangoh_data_router_dataUpdateHndlr_t* dataUpdateHndlr = (swi_mangoh_data_router_dataUpdateHndlr_t*)addHandlerRef;

    LE_INFO("app('%s') --> unsubscribe key('%s')", appName, dataUpdateHndlr->key);
    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_getDataItem(&dataRouter.db, dataUpdateHndlr->key);
    if (dbItem)
    {
      le_sls_Link_t* linkPtr = le_sls_Peek(&dbItem->subscribers);
      while (linkPtr)
      {
        swi_mangoh_data_router_subscriberLink_t* subscriberElem = CONTAINER_OF(linkPtr, swi_mangoh_data_router_subscriberLink_t, link);

        if (!strcmp(subscriberElem->subscriber->appId, appName))
        {
          LE_DEBUG("key('%s') remove data handler", dataUpdateHndlr->key);
          dataUpdateHndlr = le_hashmap_Remove(subscriberElem->subscriber->dataUpdateHndlrs, dataUpdateHndlr->key);

          if (le_hashmap_isEmpty(subscriberElem->subscriber->dataUpdateHndlrs))
          {
            LE_DEBUG("key('%s') remove subscriber('%s')", dataUpdateHndlr->key, appName);
            linkPtr = le_sls_Pop(&dbItem->subscribers);
            free(subscriberElem->subscriber);
            free(subscriberElem);
          }

          free(dataUpdateHndlr);
          break;
        }

        linkPtr = le_sls_PeekNext(&dbItem->subscribers, linkPtr);
      }
    }
    else
    {
      LE_WARN("key('%s') does not exist", dataUpdateHndlr->key);
    }
  }

cleanup:
  return;
}

COMPONENT_INIT
{
  LE_INFO("MangOH Data Router Service Starting");

  swi_mangoh_data_router_db_init(&dataRouter.db);

  dataRouter.sessions = le_hashmap_Create(SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_NAME, SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_SIZE, le_hashmap_HashString, le_hashmap_EqualsString);
  dataRouter.protocolType = SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT;

  le_sig_Block(SIGTERM);
  le_sig_SetEventHandler(SIGTERM, swi_mangoh_data_router_SigTermEventHandler);

  le_arg_SetStringCallback (swi_mangoh_data_router_selectAvProtocol,
      SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_SHORT_NAME,
      SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_LONG_NAME);	
}
