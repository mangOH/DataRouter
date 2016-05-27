/**
 * This program provides a way to interact with the data router from the command line.  It is not
 * currently possible to supply connection parameters such as push to AirVantage, the password or
 * URL from the command line.  Instead the dataRouter_SessionStart() call in COMPONENT_INIT should
 * be modified and the software rebuilt.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */

#include "legato.h"
#include "interfaces.h"
#include "le_args.h"
#include <stdlib.h>
#include <stdio.h>

static const char cmdGet[] = "get";
static const char cmdSet[] = "set";
static const char cmdMonitor[] = "monitor";

#define TYPE_CHAR_BOOLEAN ('b')
#define TYPE_CHAR_INTEGER ('i')
#define TYPE_CHAR_FLOATING_POINT ('f')
#define TYPE_CHAR_STRING ('s')

struct Value
{
    dataRouter_DataType_t type;
    union
    {
        bool b;
        int32_t i;
        float f;
        const char* s;
    } data;

};

static const char usageMsg[] = "\
NAME:\n\
    %s - mangOH DataRouter utility\n\
\n\
SYNOPSIS:\n\
    %s --help \n\
    %s get <key>\n\
    %s set <key> <type>:<value>\n\
    %s monitor <key>\n\
\n\
DESCRIPTION:\n\
    get:\n\
        Retrieves the value from the data router with the given key.\n\
\n\
    set:\n\
        Sets the value for the given key.  See SPECIFYING VALUES.\n\
\n\
    monitor:\n\
        Watch the given key for updates and print them out similar to the get\n\
        operation.  This command will never exit.\n\
\n\
SPECIFYING VALUES:\n\
    All types supported by the data router are supported.\n\
        Boolean - 'b:true' or 'b:false'\n\
        Integer - 'i:19' or 'i:-81'\n\
        Floating Point - 'f:17.123' or 'f:-3.14159'\n\
        String - 's:some string'\n\
";

//--------------------------------------------------------------------------------------------------
/**
 * Print the usage message for this program and then terminates the program
 */
//--------------------------------------------------------------------------------------------------
static void PrintUsage(
    FILE* stream,            ///< stream to print the usage message to
    const char* message,     ///< message to print before the main usage message.  Typically an
                             ///  error message to supply to the user.
    int exitCode             ///< exit code to exit with
)
{
    if (message != NULL)
    {
        fputs(message, stream);
        fputs("\n", stream);
    }

    const char* programName = le_arg_GetProgramName();

    fprintf(
        stream,
        usageMsg,
        programName,
        programName,
        programName,
        programName,
        programName);

    exit(exitCode);
}

//--------------------------------------------------------------------------------------------------
/**
 * Parse a value string
 *
 * @return
 *      true if the value is parsed successfully.
 *
 * @note
 *      When parsing a string, the Value struct's data.s member will point at the input string so
 *      care needs to be taken to make sure that the input string doesn't go out of scope after
 *      calling this function, but before discarding the Value struct.
 */
//--------------------------------------------------------------------------------------------------
static bool ParseValue(
    const char* valueStr, ///< [IN] string to parse from.  The string is expected to be in the form
                          ///  TYPE_CHAR_?:VALUE.
    struct Value* value   ///< [OUT] data structure to store parsed data
)
{
    int minValueLen = 3; // type, colon, value
    if (strlen(valueStr) < minValueLen)
    {
        return false;
    }

    if (valueStr[1] != ':')
    {
        return false;
    }

    const char* valuePart = &(valueStr[2]);
    switch (valueStr[0])
    {
        case TYPE_CHAR_BOOLEAN:
        {
            value->type = DATAROUTER_BOOLEAN;
            if (strcmp(valuePart, "true") == 0)
            {
                value->data.b = true;
            }
            else if (strcmp(valuePart, "false") == 0)
            {
                value->data.b = false;
            }
            else
            {
                return false;
            }
            break;
        }

        case TYPE_CHAR_INTEGER:
        {
            value->type = DATAROUTER_INTEGER;
            // sscanf will ignore leading space, but we don't want to
            if (isspace(valuePart[0]))
            {
                return false;
            }
            int i;
            int charsConsumed;
            sscanf(valuePart, "%d%n", &i, &charsConsumed);
            if (charsConsumed != strlen(valuePart))
            {
                // Not all of the value was consumed
                return false;
            }
            value->data.i = i;
            break;
        }

        case TYPE_CHAR_FLOATING_POINT:
        {
            value->type = DATAROUTER_FLOAT;
            // sscanf will ignore leading space, but we don't want to
            if (isspace(valuePart[0]))
            {
                return false;
            }
            double f;
            int charsConsumed;
            sscanf(valuePart, "%lf%n", &f, &charsConsumed);
            if (charsConsumed != strlen(valuePart))
            {
                // Not all of the value was consumed
                return false;
            }
            value->data.f = f;
            break;
        }

        case TYPE_CHAR_STRING:
        {
            value->type = DATAROUTER_STRING;
            value->data.s = valuePart;
            break;
        }

        default:
        {
            return false;
            break;
        }
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
/**
 * Perform escaping required to create a JSON string from a C string.
 *
 * For example:
 * @code
 *   const char s[] = "See my \"hello world\" in c:\hello.c";
 *   char json[64];
 *   ToJsonString(s, json);
 *   puts(json);
 * @endcode
 * Will print out: "See my \"hello world\" in c:\\hello.c"
 */
//--------------------------------------------------------------------------------------------------
static void ToJsonString(
    const char* in, ///< [IN] string to convert into a JSON string
    char* out       ///< [OUT] buffer to write converted JSON string into.  To handle the worst
                    ///  case, length(out) should be >= (2 * (length(in) - 1)) + 1 because possibly
                    ///  every character will expand into 2 characters except for the terminating
                    ///  '\0'.
)
{
    size_t outIndex = 0;
    out[outIndex++] = '"';
    size_t sLength = strlen(in);
    for (size_t i = 0; i < sLength; i++)
    {
        if (in[i] == '"' || in[i] == '\\')
        {
            out[outIndex++] = '\\';
        }
        out[outIndex++] = in[i];
    }
    out[outIndex++] = '"';
    out[outIndex++] = '\0';
}

//--------------------------------------------------------------------------------------------------
/**
 * Prints a pre-formatted value along with the key and timestamp
 *
 * @note
 *      value should be a valid JSON value
 */
//--------------------------------------------------------------------------------------------------
static void PrintValue(
    const char* key,   ///< [IN] Key to print as a regular C string
    const char* value, ///< [IN] Value to print that should be a valid JSON value
    uint32_t timestamp ///< [IN] Timestamp of the value
)
{
    char jsonKey[256];
    ToJsonString(key, jsonKey);
    printf("{ \"key\":%s, \"value\":%s, \"timestamp\":%d }\n", jsonKey, value, timestamp);
}

//--------------------------------------------------------------------------------------------------
/**
 * Get a value associated with a key and print it out
 *
 * @note
 *      There is currently no way to verify that a value exists in the data router or query what
 *      type it is so garbage will be printed if a non-existing key is specified or if the wrong
 *      type is specified.
 */
//--------------------------------------------------------------------------------------------------
static void GetAndPrint(
    const char* key,     ///< [IN] Key to get value for
    const char* typeStr  ///< [IN] Type of the value to get and print
)
{
    if (strlen(typeStr) != 1)
    {
        PrintUsage(stderr, "Type sepcifier is not of length 1\n", EXIT_FAILURE);
    }

    uint32_t timestamp;
    switch (typeStr[0])
    {
        case TYPE_CHAR_BOOLEAN:
        {
            bool b;
            dataRouter_ReadBoolean(key, &b, &timestamp);
            PrintValue(key, b ? "true" : "false", timestamp);
            break;
        }

        case TYPE_CHAR_INTEGER:
        {
            int32_t i;
            dataRouter_ReadInteger(key, &i, &timestamp);
            char iStr[32];
            sprintf(iStr, "%d", i);
            PrintValue(key, iStr, timestamp);
            break;
        }

        case TYPE_CHAR_FLOATING_POINT:
        {
            float f;
            dataRouter_ReadFloat(key, &f, &timestamp);
            char fStr[32];
            sprintf(fStr, "%f", f);
            PrintValue(key, fStr, timestamp);
            break;
        }

        case TYPE_CHAR_STRING:
        {
            char s[128];
            dataRouter_ReadString(key, s, sizeof(s), &timestamp);
            char jsonString[256];
            ToJsonString(s, jsonString);
            PrintValue(key, jsonString, timestamp);
            break;
        }

        default:
        {
            PrintUsage(stderr, "Invalid type specified\n", EXIT_FAILURE);
            break;
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Get a value, print it and then exit the program
 */
//--------------------------------------------------------------------------------------------------
static void performGet(
    const char* key,     ///< [IN] Key to get value for
    const char* typeStr  ///< [IN] Type of the value to get and print
)
{
    GetAndPrint(key, typeStr);
    //exit(EXIT_SUCCESS); // See comment at end of COMPONENT_INIT
}

//--------------------------------------------------------------------------------------------------
/**
 * Set the value associated with the given key to the given data
 */
//--------------------------------------------------------------------------------------------------
static void performSet(
    const char* key,   ///< [IN] Key to set value for
    const char* value  ///< [IN] Value to set into for the given key.  The value string is expected
                       ///  to be in the form TYPE_CHAR_?:VALUE.
)
{
    struct Value v;
    if (!ParseValue(value, &v))
    {
        PrintUsage(stderr, "Could not parse value\n", EXIT_FAILURE);
    }

    const uint32_t now = time(NULL);
    if (v.type == DATAROUTER_BOOLEAN)
    {
        dataRouter_WriteBoolean(key, v.data.b, now);
    }
    else if (v.type == DATAROUTER_INTEGER)
    {
        dataRouter_WriteInteger(key, v.data.i, now);
    }
    else if (v.type == DATAROUTER_FLOAT)
    {
        dataRouter_WriteFloat(key, v.data.f, now);
    }
    else if (v.type == DATAROUTER_STRING)
    {
        dataRouter_WriteString(key, v.data.s, now);
    }

    //exit(EXIT_SUCCESS); // See comment at end of COMPONENT_INIT
}

//--------------------------------------------------------------------------------------------------
/**
 * Convert a type definition supported by the data router into a string representing the type
 */
//--------------------------------------------------------------------------------------------------
static void TypeToTypeStr(
    dataRouter_DataType_t type, ///< [IN] Type to convert
    char* typeStr               ///< [OUT] A string of length >= 2 where the output will be
                                ///  written.  The output string will be empty if an unsupported
                                ///  input is provided.
)
{
    typeStr[1] = '\0';
    switch (type)
    {
        case DATAROUTER_BOOLEAN:
            typeStr[0] = TYPE_CHAR_BOOLEAN;
            break;

        case DATAROUTER_INTEGER:
            typeStr[0] = TYPE_CHAR_INTEGER;
            break;

        case DATAROUTER_FLOAT:
            typeStr[0] = TYPE_CHAR_FLOATING_POINT;
            break;

        case DATAROUTER_STRING:
            typeStr[0] = TYPE_CHAR_STRING;
            break;

        default:
            typeStr[0] = '\0';
            break;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * An update handler which prints out the key/value/timestamp
 */
//--------------------------------------------------------------------------------------------------
static void MonitorUpdateHandler(
    dataRouter_DataType_t type, ///< [IN] Type of the value that has changed
    const char* key,            ///< [IN] Key of the value that has changed
    void* contextPtr            ///< [IN] context pointer - unused
)
{
    char typeStr[2];
    TypeToTypeStr(type, typeStr);
    GetAndPrint(key, typeStr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Monitor a given key for updates and print them out via an update handler
 */
//--------------------------------------------------------------------------------------------------
static void performMonitor(
    const char* key  ///< [IN] Key of data element to monitor
)
{
    dataRouter_AddDataUpdateHandler(key, MonitorUpdateHandler, NULL);
}


COMPONENT_INIT
{

    // Persist data in local storage, but don't push to AirVantage
    dataRouter_SessionStart("", "", false, DATAROUTER_PERSIST);

    // Cache data only.  Data will be lost when the data router exitsa.  Data is not pushed to
    // AirVantage.
    //dataRouter_SessionStart("", "", false, DATAROUTER_CACHE);

    // Persist data locally and also push to AirVantage.  Update the password.
    //dataRouter_SessionStart("eu.airvantage.net", "Your password", true, DATAROUTER_PERSIST);

    // Cache data locally only, but push data to AirVantage.  Update the password.
    //dataRouter_SessionStart("eu.airvantage.net", "Your password", true, DATAROUTER_CACHE);

    size_t numArgs = le_arg_NumArgs();
    if (numArgs == 0)
    {
        PrintUsage(stderr, NULL, EXIT_FAILURE);
    }

    if (le_arg_GetFlagOption("h", "help") == LE_OK)
    {
        PrintUsage(stdout, NULL, EXIT_SUCCESS);
    }

    const char* arg0 = le_arg_GetArg(0);
    if (strcmp(arg0, cmdGet) == 0)
    {
        if (numArgs != 3)
        {
            PrintUsage(stderr, "Wrong number of arguments to 'get'", EXIT_FAILURE);
        }
        performGet(le_arg_GetArg(1), le_arg_GetArg(2));
    }
    else if (strcmp(arg0, cmdSet) == 0)
    {
        if (numArgs != 3)
        {
            PrintUsage(stderr, "Wrong number of arguments to 'set'", EXIT_FAILURE);
        }
        performSet(le_arg_GetArg(1), le_arg_GetArg(2));
    }
    else if (strcmp(arg0, cmdMonitor) == 0)
    {
        if (numArgs != 2)
        {
            PrintUsage(stderr, "Wrong number of arguments to 'monitor'", EXIT_FAILURE);
        }
        performMonitor(le_arg_GetArg(1));
    }
    else
    {
        char message[64];
        char shortenedArg[32] = {0};
        size_t argLen = strlen(arg0);
        size_t toCopy = sizeof(shortenedArg) - 1;
        if (argLen > sizeof(shortenedArg) - 1)
        {
            shortenedArg[sizeof(shortenedArg) - 1] = '.';
            shortenedArg[sizeof(shortenedArg) - 2] = '.';
            shortenedArg[sizeof(shortenedArg) - 3] = '.';
            toCopy -= 3;
        }
        strncpy(shortenedArg, arg0, toCopy);
        snprintf(message, sizeof(message), "Unsupported command '%s'\n", shortenedArg);
        PrintUsage(stderr, message, EXIT_FAILURE);
    }


    // TODO: There seems to be a problem where if the session is closed before the data is pushed
    // to AirVantage, the update is lost.  Leave the session open for now and wait for the user to
    // kill the process.
    dataRouter_SessionEnd();
}
