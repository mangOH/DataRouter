#include "interfaces.h"
#include "legato.h"
#include "db.h"

static void swi_mangoh_data_router_db_restorePersistedData(swi_mangoh_data_router_db_t*);
static void swi_mangoh_data_router_db_restoreEncryptedData(swi_mangoh_data_router_db_t*);

static void swi_mangoh_data_router_db_restoreEncryptedData(swi_mangoh_data_router_db_t* db)
{
  char* encryptedKeys = NULL;
  swi_mangoh_data_router_dbItem_t* dbItem = NULL;
  size_t len = 0;
  le_result_t res = LE_OK;

  LE_ASSERT(db);

  encryptedKeys = calloc(1, SWI_MANGOH_DATA_ROUTER_SEC_STORE_MAX_KEYS_LEN);
  if (!encryptedKeys)
  {
    LE_ERROR("ERROR calloc() failed");
    goto cleanup;
  }

  res = le_secStore_Read(SWI_MANGOH_DATA_ROUTER_SEC_STORE_BASE_NAME, (uint8_t*)encryptedKeys, &len);
  if (res != LE_OK)
  {
    LE_ERROR("ERROR le_secStore_Read() failed(%d)", res);
    goto cleanup;
  }

  if (len)
  {
    char* key = strtok(encryptedKeys, SWI_MANGOH_DATA_ROUTER_SEC_STORE_KEYS_SEPARATOR);
    while (key)
    {
      dbItem = swi_mangoh_data_router_db_createDataItem(db, key);
      if (!dbItem)
      {
        LE_ERROR("ERROR swi_mangoh_data_router_db_createDataItem() failed");
        goto cleanup;
      }

      res = le_secStore_Read(key, (uint8_t*)&dbItem->data, &len);
      if (res != LE_OK)
      {
        LE_ERROR("ERROR le_secStore_Read() failed(%d)", res);
        goto cleanup;
      }

      LE_ASSERT(len == sizeof(swi_mangoh_data_router_data_t));
      key = strtok(NULL, SWI_MANGOH_DATA_ROUTER_SEC_STORE_KEYS_SEPARATOR);
    }
  }

cleanup:
  if (res != LE_OK) free(dbItem);
  free(encryptedKeys);
}

static void swi_mangoh_data_router_db_restorePersistedData(swi_mangoh_data_router_db_t* db)
{
  le_cfg_IteratorRef_t iterRef = le_cfg_CreateReadTxn(SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME);
  le_result_t res = le_cfg_GoToFirstChild(iterRef);

  LE_ASSERT(db);

  while (res == LE_OK)
  {
    char key[SWI_MANGOH_DATA_ROUTER_KEY_MAX_LEN] = {0};

    res = le_cfg_GetString(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_KEY, key, sizeof(key), "");
    if (res != LE_OK)
    {
      LE_ERROR("ERROR le_cfg_GetString() failed(%d)", res);
      goto cleanup;
    }

    swi_mangoh_data_router_dbItem_t* dbItem = swi_mangoh_data_router_db_createDataItem(db, key);
    if (!dbItem)
    {
      LE_ERROR("ERROR swi_mangoh_data_router_db_createDataItem() failed");
      goto cleanup;
    }

    dbItem->data.type = le_cfg_GetInt(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_TYPE, 0);
    switch (dbItem->data.type)
    {
    case DATAROUTER_BOOLEAN:
      dbItem->data.bValue = le_cfg_GetBool(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_VALUE, false);
      LE_DEBUG("restore(%u) key('%s'), value('%s')", dbItem->storageType, dbItem->data.key, dbItem->data.bValue ? "true":"false");
      break;

    case DATAROUTER_INTEGER:
      dbItem->data.iValue = le_cfg_GetInt(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_VALUE, 0);
      LE_DEBUG("restore(%u) key('%s'), value(%d)", dbItem->storageType, dbItem->data.key, dbItem->data.iValue);
      break;

    case DATAROUTER_FLOAT:
      dbItem->data.fValue = le_cfg_GetFloat(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_VALUE, 0.0);
      LE_DEBUG("restore(%u) key('%s'), value(%f)", dbItem->storageType, dbItem->data.key, dbItem->data.fValue);
      break;

    case DATAROUTER_STRING:
      res = le_cfg_GetString(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_VALUE, dbItem->data.sValue, sizeof(dbItem->data.sValue), "");
      if (res != LE_OK)
      {
        LE_ERROR("ERROR le_cfg_GetString() failed(%d)", res);
        goto cleanup;
      }

      LE_DEBUG("restore(%u) key('%s'), value('%s')", dbItem->storageType, dbItem->data.key, dbItem->data.sValue);
      break;
    }

    dbItem->data.timestamp = le_cfg_GetInt(iterRef, SWI_MANGOH_DATA_ROUTER_CFG_TIMESTAMP, 0);

    res = le_cfg_GoToNextSibling(iterRef);
  }

  le_cfg_CommitTxn(iterRef);
  le_cfg_QuickDeleteNode(SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME);

cleanup:
  return;
}

swi_mangoh_data_router_dbItem_t* swi_mangoh_data_router_db_createDataItem(swi_mangoh_data_router_db_t* db, const char* key)
{
  swi_mangoh_data_router_dbItem_t* dbItem = NULL;
  swi_mangoh_data_router_dbItem_t* ret = NULL;

  LE_ASSERT(db);
  LE_ASSERT(key);

  dbItem = calloc(1, sizeof(swi_mangoh_data_router_dbItem_t));
  if (!dbItem)
  {
    LE_ERROR("ERROR calloc() failed");
    goto cleanup;
  }

  LE_DEBUG("create data item('%s')", key);
  strcpy(dbItem->data.key, key);
  dbItem->subscribers = LE_SLS_LIST_INIT;

  ret = le_hashmap_Put(db->database, dbItem->data.key, dbItem);
  if (ret)
  {
    LE_WARN("le_hashmap_Put() replaced key(''%s')", dbItem->data.key);
    free(ret);
    goto cleanup;
  }

cleanup:
  return dbItem;
}

swi_mangoh_data_router_dbItem_t* swi_mangoh_data_router_db_getDataItem(swi_mangoh_data_router_db_t* db, const char* key)
{
  LE_ASSERT(db);
  LE_ASSERT(key);
  return le_hashmap_Get(db->database, key);
}

void swi_mangoh_data_router_db_setStorageType(swi_mangoh_data_router_dbItem_t* dbItem, dataRouter_Storage_t storageType)
{
  LE_ASSERT(dbItem);
  dbItem->storageType = storageType;
}

void swi_mangoh_data_router_db_setDataType(swi_mangoh_data_router_dbItem_t* dbItem, dataRouter_DataType_t dataType)
{
  LE_ASSERT(dbItem);
  dbItem->data.type = dataType;
}

void swi_mangoh_data_router_db_setBooleanValue(swi_mangoh_data_router_dbItem_t* dbItem, bool value)
{
  LE_ASSERT(dbItem);
  dbItem->data.bValue = value;
}

void swi_mangoh_data_router_db_setIntegerValue(swi_mangoh_data_router_dbItem_t* dbItem, int32_t value)
{
  LE_ASSERT(dbItem);
  dbItem->data.iValue = value;
}

void swi_mangoh_data_router_db_setFloatValue(swi_mangoh_data_router_dbItem_t* dbItem, float value)
{
  LE_ASSERT(dbItem);
  dbItem->data.fValue = value;
}

void swi_mangoh_data_router_db_setStringValue(swi_mangoh_data_router_dbItem_t* dbItem, const char* value)
{
  LE_ASSERT(dbItem);
  strcpy(dbItem->data.sValue, value);
}

void swi_mangoh_data_router_db_setTimestamp(swi_mangoh_data_router_dbItem_t* dbItem, uint32_t timestamp)
{
  LE_ASSERT(dbItem);
  dbItem->data.timestamp = timestamp;
}

void swi_mangoh_data_router_db_init(swi_mangoh_data_router_db_t* db)
{
  LE_ASSERT(db);

  db->database = le_hashmap_Create(SWI_MANGOH_DATA_ROUTER_DB_MAP_NAME, SWI_MANGOH_DATA_ROUTER_DB_MAP_SIZE, le_hashmap_HashString, le_hashmap_EqualsString);
  swi_mangoh_data_router_db_restorePersistedData(db);
  swi_mangoh_data_router_db_restoreEncryptedData(db);
}

void swi_mangoh_data_router_db_destroy(swi_mangoh_data_router_db_t* db)
{
  char* encryptedKeys = NULL;
  uint32_t encryptedKeysLen = sizeof('\0');

  LE_ASSERT(db);

  if (!le_hashmap_isEmpty(db->database))
  {
    le_hashmap_It_Ref_t iter = le_hashmap_GetIterator(db->database);

    encryptedKeys = calloc(1, SWI_MANGOH_DATA_ROUTER_SEC_STORE_MAX_KEYS_LEN);
    if (!encryptedKeys)
    {
      LE_ERROR("ERROR calloc() failed");
      goto cleanup;
    }

    int32_t res = le_hashmap_NextNode(iter);
    while (res == LE_OK)
    {
      swi_mangoh_data_router_dbItem_t* dbItem = (swi_mangoh_data_router_dbItem_t*)le_hashmap_GetValue(iter);
      if (dbItem)
      {
        switch (dbItem->storageType)
        {
        case DATAROUTER_PERSIST:
        {
          char path[SWI_MANGOH_DATA_ROUTER_CFG_MAX_PATH_LEN] = {0};

          snprintf(path, SWI_MANGOH_DATA_ROUTER_CFG_MAX_PATH_LEN, "%s/%s/%s", SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME, dbItem->data.key, SWI_MANGOH_DATA_ROUTER_CFG_KEY);
          le_cfg_QuickSetString(path, dbItem->data.key);

          snprintf(path, SWI_MANGOH_DATA_ROUTER_CFG_MAX_PATH_LEN, "%s/%s/%s", SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME, dbItem->data.key, SWI_MANGOH_DATA_ROUTER_CFG_TYPE);
          le_cfg_QuickSetInt(path, dbItem->data.type);

          snprintf(path, SWI_MANGOH_DATA_ROUTER_CFG_MAX_PATH_LEN, "%s/%s/%s", SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME, dbItem->data.key, SWI_MANGOH_DATA_ROUTER_CFG_VALUE);
          switch (dbItem->data.type)
          {
          case DATAROUTER_BOOLEAN:
            LE_DEBUG("store(%u) key('%s'), value('%s')", dbItem->storageType, dbItem->data.key, dbItem->data.bValue ? "true":"false");
            le_cfg_QuickSetBool(path, dbItem->data.bValue);
            break;

          case DATAROUTER_INTEGER:
            LE_DEBUG("store(%u) key('%s'), value(%d)", dbItem->storageType, dbItem->data.key, dbItem->data.iValue);
            le_cfg_QuickSetInt(path, dbItem->data.iValue);
            break;

          case DATAROUTER_FLOAT:
            LE_DEBUG("store(%u) key('%s'), value(%f)", dbItem->storageType, dbItem->data.key, dbItem->data.fValue);
            le_cfg_QuickSetFloat(path, dbItem->data.fValue);
            break;

          case DATAROUTER_STRING:
            LE_DEBUG("store(%u) key('%s'), value('%s')", dbItem->storageType, dbItem->data.key, dbItem->data.sValue);
            le_cfg_QuickSetString(path, dbItem->data.sValue);
            break;
          }

          snprintf(path, SWI_MANGOH_DATA_ROUTER_CFG_MAX_PATH_LEN, "%s/%s/%s", SWI_MANGOH_DATA_ROUTER_CFG_BASE_NAME, dbItem->data.key, SWI_MANGOH_DATA_ROUTER_CFG_TIMESTAMP);
          le_cfg_QuickSetInt(path, dbItem->data.timestamp);
          break;
        }
        case DATAROUTER_PERSIST_ENCRYPTED:
        {
          res = le_secStore_Write(dbItem->data.key, (const uint8_t*)&dbItem->data, sizeof(swi_mangoh_data_router_data_t));
          if (res != LE_OK)
          {
            LE_ERROR("ERROR le_secStore_Write() failed(%d)", res);
            goto cleanup;
          }

          uint32_t keyLen = strlen(dbItem->data.key);
          if (encryptedKeysLen + keyLen > SWI_MANGOH_DATA_ROUTER_SEC_STORE_MAX_KEYS_LEN)
          {
            LE_ERROR("ERROR maximum keys reached(%u > %u)", encryptedKeysLen + keyLen, SWI_MANGOH_DATA_ROUTER_SEC_STORE_MAX_KEYS_LEN);
            goto cleanup;
          }

          strcat(encryptedKeys, dbItem->data.key);
          strcat(encryptedKeys, SWI_MANGOH_DATA_ROUTER_SEC_STORE_KEYS_SEPARATOR);
          encryptedKeysLen += keyLen + 1;
          break;
        }
        default:
          break;
        }
      }

      res = le_hashmap_NextNode(iter);
    }

    if (encryptedKeysLen)
    {
      res = le_secStore_Write(SWI_MANGOH_DATA_ROUTER_SEC_STORE_BASE_NAME, (const uint8_t*)encryptedKeys, encryptedKeysLen);
      if (res != LE_OK)
      {
        LE_ERROR("ERROR le_secStore_Write() failed(%d)", res);
        goto cleanup;
      }
    }
  }

cleanup:
  if (encryptedKeys) free(encryptedKeys);
  return;
}

