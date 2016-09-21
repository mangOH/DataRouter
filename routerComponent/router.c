#include "interfaces.h"
#include "legato.h"
#include "router.h"
#include "list_helpers.h"

// Used to provide the session reference to match against when removing from the list of update
// handlers.
static le_msg_SessionRef_t ComparisonClientSessionRef;
static swi_mangoh_data_router_t dataRouter;

static bool IsUpdateHandlerForSession(le_sls_Link_t* link);
static void FreeDataUpdateHandlerListNode(le_sls_Link_t* link);
static void swi_mangoh_data_router_SigTermEventHandler(int);
static le_result_t swi_mangoh_data_router_getClientPidAndAppName(pid_t*, char[], size_t);
static void swi_mangoh_data_router_selectAvProtocol(const char*);
static void pushItemIfRequired(
    swi_mangoh_data_router_session_t* session,
    const char* key,
    const swi_mangoh_data_router_dbItem_t* dbItem);


static bool IsUpdateHandlerForSession
(
    le_sls_Link_t* link
)
{
    swi_mangoh_data_router_dataUpdateHandler_t* node =
        CONTAINER_OF(link, swi_mangoh_data_router_dataUpdateHandler_t, next);
    return node->clientSessionRef == ComparisonClientSessionRef;
}

static void FreeDataUpdateHandlerListNode
(
    le_sls_Link_t* link
)
{
    swi_mangoh_data_router_dataUpdateHandler_t* node =
        CONTAINER_OF(link, swi_mangoh_data_router_dataUpdateHandler_t, next);
    free(node);
}

static void swi_mangoh_data_router_SigTermEventHandler
(
    int sigNum
)
{
    LE_INFO("Data router persistence started");
    swi_mangoh_data_router_db_destroy(&dataRouter.db);
    LE_INFO("Data router persistence completed");
}

static le_result_t swi_mangoh_data_router_getSessionPidAndAppName
(
    le_msg_SessionRef_t sessionRef,
    pid_t* pid,
    char appName[],
    size_t len
)
{
    le_result_t res = LE_OK;

    res = le_msg_GetClientProcessId(sessionRef, pid);
    if (res != LE_OK)
    {
        LE_ERROR("ERROR le_msg_GetClientProcessId() failed(%d)", res);
        goto cleanup;
    }

    LE_DEBUG("process(%u), len(%zu)", *pid, len);
    res = le_appInfo_GetName(*pid, appName, len);
    if (res != LE_OK)
    {
        // ensure that the string is empty
        appName[0] = '\0';
        res        = LE_OK;
    }

    LE_DEBUG("app name('%s')", appName);

cleanup:
    return res;
}

static le_result_t swi_mangoh_data_router_getClientPidAndAppName
(
    pid_t* pid,
    char appName[],
    size_t len
)
{
    return swi_mangoh_data_router_getSessionPidAndAppName(
        dataRouter_GetClientSessionRef(), pid, appName, len);
}

static void swi_mangoh_data_router_selectAvProtocol
(
    const char* value
)
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

void swi_mangoh_data_router_notifySubscribers
(
    const char* key,
    const swi_mangoh_data_router_dbItem_t* dbItem
)
{
    LE_ASSERT(key);
    LE_ASSERT(dbItem);

    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    for (le_sls_Link_t* nodePtr = le_sls_Peek(&dbItem->handlers);
         nodePtr;
         nodePtr = le_sls_PeekNext(&dbItem->handlers, nodePtr))
    {
        swi_mangoh_data_router_dataUpdateHandler_t* handlerData =
            CONTAINER_OF(nodePtr, swi_mangoh_data_router_dataUpdateHandler_t, next);

        // notify all other clients
        if (handlerData->clientSessionRef != clientSession)
        {
            LE_DEBUG("Calling update handler for key (%s) on client (%p)", key, clientSession);
            LE_ASSERT(handlerData->handler);
            handlerData->handler(dbItem->data.type, key, handlerData->context);
        }
    }
}

void dataRouter_SessionStart
(
    const char* urlAsset,
    const char* password,
    bool pushAv,
    dataRouter_Storage_t storage
)
{
    LE_ASSERT(urlAsset);
    LE_ASSERT(password);

    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();

    // Get app name and pid for display purposes
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN] = {0};
    pid_t pid = 0;
    le_result_t res =
        swi_mangoh_data_router_getClientPidAndAppName(&pid, appName, sizeof(appName));
    if (res != LE_OK)
    {
        LE_ERROR("ERROR swi_mangoh_data_router_getClientPidAndAppName() failed(%d)", res);
        goto cleanup;
    }

    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (!session)
    {
        session = calloc(1, sizeof(swi_mangoh_data_router_session_t));
        if (!session)
        {
            LE_ERROR("ERROR calloc() failed");
            goto cleanup;
        }

        session->pushAv = pushAv;
        session->storageType = storage;

        if (session->pushAv)
        {
            switch (dataRouter.protocolType)
            {
                case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
                    swi_mangoh_data_router_mqttSessionStart(
                        appName, urlAsset, password, &session->mqtt, &dataRouter.db);
                    break;

                case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
                    swi_mangoh_data_router_avSvcSessionStart(
                        urlAsset, &session->avsvc, &dataRouter.db);
                    break;

                case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
                    break;

                default:
                    LE_WARN("unsupported protocol(%u)", dataRouter.protocolType);
                    break;
            }
        }

        if (le_hashmap_Put(dataRouter.sessions, clientSession, session))
        {
            LE_ERROR("le_hashmap_Put() failed");
            goto cleanup;
        }

        LE_DEBUG("added session('%p')", clientSession);
    }
    else
    {
        LE_WARN(
            "data router session already exists for client session ('%p') established by app %s "
            "from pid %u",
            clientSession,
            appName,
            pid);
    }

cleanup:
    return;
}

static void swi_mangoh_data_router_removeAllUpdateHandlersForSession
(
    swi_mangoh_data_router_db_t* db,
    le_msg_SessionRef_t sessionRef
)
{
    // Iterate over all of the dbItems in db->database.  We don't care which key is associated with
    // the item.
    le_hashmap_It_Ref_t iter   = le_hashmap_GetIterator(db->database);
    ComparisonClientSessionRef = sessionRef;
    while (le_hashmap_NextNode(iter) == LE_OK)
    {
        swi_mangoh_data_router_dbItem_t* dbItem =
            (swi_mangoh_data_router_dbItem_t*)le_hashmap_GetValue(iter);
        LE_ASSERT(dbItem);

        ListRemoveFirstMatch(
            &dbItem->handlers, &IsUpdateHandlerForSession, &FreeDataUpdateHandlerListNode);
    }
}

static void swi_mangoh_data_router_cleanupSession
(
    le_msg_SessionRef_t clientSession
)
{
    LE_DEBUG("Cleaning up session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        if (session->pushAv)
        {
            switch (dataRouter.protocolType)
            {
                case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
                    if (swi_mangoh_data_router_mqttSessionEnd(&session->mqtt))
                    {
                        if (!le_hashmap_Remove(dataRouter.sessions, clientSession))
                        {
                            LE_ERROR("ERROR le_hashmap_Remove() failed");
                            free(session);
                            goto cleanup;
                        }

                        free(session);
                    }
                    else
                    {
                        // TODO: it seems like the session will never be removed from the hashmap
                        // in this case.  This is a memory leak.
                    }
                    break;

                case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
                    swi_mangoh_data_router_avSvcSessionEnd(&session->avsvc);

                    if (!le_hashmap_Remove(dataRouter.sessions, clientSession))
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
            if (!le_hashmap_Remove(dataRouter.sessions, clientSession))
            {
                LE_ERROR("ERROR le_hashmap_Remove() failed");
                free(session);
                goto cleanup;
            }
        }
    }
    else
    {
        LE_WARN("session('%p') not found", clientSession);
    }

    // Make sure that all of the update handlers are removed
    swi_mangoh_data_router_removeAllUpdateHandlersForSession(&dataRouter.db, clientSession);

cleanup:

    return;
}

static void swi_mangoh_data_router_onSessionClosed
(
    le_msg_SessionRef_t sessionRef,
    void* contextPtr
)
{
    LE_DEBUG("dataRouter session closed due to client disconnect");
    swi_mangoh_data_router_cleanupSession(sessionRef);
}

void dataRouter_SessionEnd
(
    void
)
{
    LE_DEBUG("dataRouter session closed explicitly");
    swi_mangoh_data_router_cleanupSession(dataRouter_GetClientSessionRef());
}

void dataRouter_WriteBoolean
(
    const char* key,
    bool value,
    uint32_t timestamp
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        LE_DEBUG(
            "app(%s)/pid(%u)/session(%p) --> key(%s) = value(%d), timestamp(%u)",
            appName,
            pid,
            clientSession,
            key,
            value,
            timestamp);

        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
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

        pushItemIfRequired(session, key, dbItem);

        swi_mangoh_data_router_notifySubscribers(key, dbItem);
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_WriteInteger
(
    const char* key,
    int32_t value,
    uint32_t timestamp
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        LE_DEBUG(
            "app(%s)/pid(%u)/session(%p) --> key(%s) = value(%d), timestamp(%u)",
            appName,
            pid,
            clientSession,
            key,
            value,
            timestamp);

        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
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

        pushItemIfRequired(session, key, dbItem);

        swi_mangoh_data_router_notifySubscribers(key, dbItem);
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_WriteFloat
(
    const char* key,
    double value,
    uint32_t timestamp
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        LE_DEBUG(
            "app(%s)/pid(%u)/session(%p) --> key(%s) = value(%f), timestamp(%u)",
            appName,
            pid,
            clientSession,
            key,
            value,
            timestamp);

        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
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

        pushItemIfRequired(session, key, dbItem);

        swi_mangoh_data_router_notifySubscribers(key, dbItem);
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_WriteString
(
    const char* key,
    const char* value,
    uint32_t timestamp
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        LE_DEBUG(
            "app(%s)/pid(%u)/session(%p) --> key(%s) = value('%s'), timestamp(%u)",
            appName,
            pid,
            clientSession,
            key,
            value,
            timestamp);

        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
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

        pushItemIfRequired(session, key, dbItem);

        swi_mangoh_data_router_notifySubscribers(key, dbItem);
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_ReadBoolean
(
    const char* key,
    bool* valuePtr,
    uint32_t* timestampPtr
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
        if (dbItem)
        {
            if (dbItem->data.type == DATAROUTER_BOOLEAN)
            {
                *valuePtr     = dbItem->data.bValue;
                *timestampPtr = dbItem->data.timestamp;
                LE_DEBUG(
                    "app(%s)/pid(%u)/session(%p) <-- key(%s) = value(%u), timestamp(%u)",
                    appName,
                    pid,
                    clientSession,
                    key,
                    *valuePtr,
                    *timestampPtr);
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
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_ReadInteger
(
    const char* key,
    int32_t* valuePtr,
    uint32_t* timestampPtr
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
        if (dbItem)
        {
            if (dbItem->data.type == DATAROUTER_INTEGER)
            {
                *valuePtr     = dbItem->data.iValue;
                *timestampPtr = dbItem->data.timestamp;
                LE_DEBUG(
                    "app(%s)/pid(%u)/session(%p) <-- key(%s) = value(%d), timestamp(%u)",
                    appName,
                    pid,
                    clientSession,
                    key,
                    *valuePtr,
                    *timestampPtr);
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
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_ReadFloat
(
    const char* key,
    double* valuePtr,
    uint32_t* timestampPtr
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
        if (dbItem)
        {
            if (dbItem->data.type == DATAROUTER_FLOAT)
            {
                *valuePtr     = dbItem->data.fValue;
                *timestampPtr = dbItem->data.timestamp;
                LE_DEBUG(
                    "app(%s)/pid(%u)/session(%p) <-- key(%s) = value(%f), timestamp(%u)",
                    appName,
                    pid,
                    clientSession,
                    key,
                    *valuePtr,
                    *timestampPtr);
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
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

void dataRouter_ReadString
(
    const char* key,
    char* valuePtr,
    size_t numValues,
    uint32_t* timestampPtr
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
        if (dbItem)
        {
            if (dbItem->data.type == DATAROUTER_STRING)
            {
                memset(valuePtr, 0, numValues);
                strncpy(valuePtr, dbItem->data.sValue, numValues - 1);
                *timestampPtr = dbItem->data.timestamp;
                LE_DEBUG(
                    "app(%s)/pid(%u)/session(%p) <-- key(%s) = value('%s'), timestamp(%u)",
                    appName,
                    pid,
                    clientSession,
                    key,
                    valuePtr,
                    *timestampPtr);
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
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

dataRouter_DataUpdateHandlerRef_t dataRouter_AddDataUpdateHandler
(
    const char* key,
    dataRouter_DataUpdateHandlerFunc_t handlerPtr,
    void* contextPtr
)
{
    dataRouter_DataUpdateHandlerRef_t updateHandlerRef = NULL;
    le_msg_SessionRef_t clientSession    = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        LE_DEBUG(
            "app(%s)/pid(%u)/session(%p): register handler on key(%s)",
            appName,
            pid,
            clientSession,
            key);
        swi_mangoh_data_router_dbItem_t* dbItem =
            swi_mangoh_data_router_db_getDataItem(&dataRouter.db, key);
        if (!dbItem)
        {
            dbItem = swi_mangoh_data_router_db_createDataItem(&dataRouter.db, key);
            if (!dbItem)
            {
                LE_ERROR("ERROR swi_mangoh_data_router_db_getDataItem() failed");
                goto cleanup;
            }
        }

        le_sls_Link_t* linkPtr = le_sls_Peek(&dbItem->handlers);
        while (linkPtr)
        {
            swi_mangoh_data_router_dataUpdateHandler_t* handlerElem =
                CONTAINER_OF(linkPtr, swi_mangoh_data_router_dataUpdateHandler_t, next);

            if (handlerElem->clientSessionRef == clientSession)
            {
                LE_WARN(
                    "app(%s)/pid(%u)/session(%p) already has a handler for key(%s)",
                    appName,
                    pid,
                    clientSession,
                    key);
                break;
            }

            linkPtr = le_sls_PeekNext(&dbItem->handlers, linkPtr);
        }

        if (linkPtr == NULL)
        {
            // No handler exists for key
            swi_mangoh_data_router_dataUpdateHandler_t* newHandlerNode =
                malloc(sizeof(swi_mangoh_data_router_dataUpdateHandler_t));
            LE_ASSERT(newHandlerNode);
            newHandlerNode->next              = LE_SLS_LINK_INIT;
            newHandlerNode->dbItemInstalledOn = dbItem;
            newHandlerNode->clientSessionRef  = clientSession;
            newHandlerNode->handler           = handlerPtr;
            newHandlerNode->context           = contextPtr;
            le_sls_Stack(&dbItem->handlers, &newHandlerNode->next);

            updateHandlerRef = (dataRouter_DataUpdateHandlerRef_t)newHandlerNode;
        }
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return updateHandlerRef;
}

void dataRouter_RemoveDataUpdateHandler
(
    dataRouter_DataUpdateHandlerRef_t updateHandlerRef
)
{
    le_msg_SessionRef_t clientSession = dataRouter_GetClientSessionRef();
    pid_t pid;
    char appName[SWI_MANGOH_DATA_ROUTER_APP_NAME_LEN];
    if (swi_mangoh_data_router_getSessionPidAndAppName(
            clientSession, &pid, appName, sizeof(appName)) != LE_OK)
    {
        LE_ERROR("Failed to get client information");
        goto cleanup;
    }
    LE_DEBUG("lookup session('%p')", clientSession);
    swi_mangoh_data_router_session_t* session = le_hashmap_Get(dataRouter.sessions, clientSession);
    if (session)
    {
        swi_mangoh_data_router_dataUpdateHandler_t* dataUpdateHandlerNode =
            (swi_mangoh_data_router_dataUpdateHandler_t*)updateHandlerRef;
        // iterate over elements of dataUpdateHandlerNode->dbItemInstalledOn->handlers to identify
        // and
        // remove the node with the matching clientSession.  Unfortunately the C language doesn't
        // support closures, so we have to set a global (ComparisonClientSessionRef) for use by
        // IsUpdateHandlerForSession
        ComparisonClientSessionRef = clientSession;
        ListRemoveFirstMatch(
            &dataUpdateHandlerNode->dbItemInstalledOn->handlers,
            &IsUpdateHandlerForSession,
            &FreeDataUpdateHandlerListNode);
    }
    else
    {
        LE_ERROR(
            "Session not found for app(%s)/pid(%u)/session(%p).  Call SessionStart() to create a "
            "session.",
            appName,
            pid,
            clientSession);
    }

cleanup:
    return;
}

//--------------------------------------------------------------------------------------------------
/**
 * Push a key/dbItem pair to AirVantage if pushing to AirVantage is enabled
 */
//--------------------------------------------------------------------------------------------------
static void pushItemIfRequired
(
    swi_mangoh_data_router_session_t* session,
    const char* key,
    const swi_mangoh_data_router_dbItem_t* dbItem
)
{
    if (session->pushAv)
    {
        switch (dataRouter.protocolType)
        {
            case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT:
                swi_mangoh_data_router_mqttWrite(key, dbItem, &session->mqtt);
                break;

            case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_LWM2M:
                swi_mangoh_data_router_avSvcWrite(key, dbItem, &session->avsvc);
                break;

            case SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_NONE:
                break;

            default:
                LE_ERROR("unsupported protocol(%u)", dataRouter.protocolType);
                break;
        }
    }
}



COMPONENT_INIT
{
    LE_INFO("MangOH Data Router Service Starting");

    swi_mangoh_data_router_db_init(&dataRouter.db);

    le_msg_AddServiceCloseHandler(
        dataRouter_GetServiceRef(), swi_mangoh_data_router_onSessionClosed, NULL);

    dataRouter.sessions = le_hashmap_Create(
        SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_NAME,
        SWI_MANGOH_DATA_ROUTER_SESSIONS_MAP_SIZE,
        le_hashmap_HashVoidPointer,
        le_hashmap_EqualsVoidPointer);
    dataRouter.protocolType = SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_MQTT;

    le_sig_Block(SIGTERM);
    le_sig_SetEventHandler(SIGTERM, swi_mangoh_data_router_SigTermEventHandler);

    le_arg_SetStringCallback(
        swi_mangoh_data_router_selectAvProtocol,
        SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_SHORT_NAME,
        SWI_MANGOH_DATA_ROUTER_AV_PROTOCOL_PARAM_LONG_NAME);
}
