#ifndef _LOG_ENUM_
#define _LOG_ENUM_

#ifdef __cplusplus
extern "C" {
#endif

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// IMPORTANT: increment this variable if you make ANY changes
// to the enum below
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#define LOG_VERSION 0

// The possible events for the RAM log
// If you add an item here, don't forget to
// add it to gLogEventStrings (in log_strings.cpp) also.
typedef enum {
    // Log points required for all builds, do not change
    EVENT_NONE,
    EVENT_BUILD_TIME_UNIX_FORMAT,
    EVENT_CURRENT_TIME_UTC,
    EVENT_LOG_START,
    EVENT_LOG_STOP,
    EVENT_LOG_FILES_TO_UPLOAD,
    EVENT_LOG_UPLOAD_STARTING,
    EVENT_LOG_FILE_BYTE_COUNT,
    EVENT_LOG_FILE_UPLOAD_COMPLETED,
    EVENT_LOG_UPLOAD_TASK_COMPLETED,
    EVENT_LOG_FILE_OPEN,
    EVENT_LOG_FILE_OPEN_FAILURE,
    EVENT_LOG_FILE_CLOSE,
    EVENT_FILE_OPEN,
    EVENT_FILE_OPEN_FAILURE,
    EVENT_FILE_CLOSE,
    EVENT_FILE_DELETED,
    EVENT_FILE_DELETE_FAILURE,
    EVENT_DIR_OPEN,
    EVENT_DIR_OPEN_FAILURE,
    EVENT_DIR_SIZE,
    EVENT_DNS_LOOKUP,
    EVENT_DNS_LOOKUP_FAILURE,
    EVENT_SOCKET_OPENING,
    EVENT_SOCKET_OPENING_FAILURE,
    EVENT_SOCKET_OPENED,
    EVENT_TCP_CONNECTING,
    EVENT_TCP_CONNECT_FAILURE,
    EVENT_TCP_CONNECTED,
    EVENT_TCP_CONFIGURED,
    EVENT_TCP_CONFIGURATION_FAILURE,
    EVENT_SEND_START,
    EVENT_SEND_STOP,
    EVENT_SEND_FAILURE,
    EVENT_SOCKET_BAD,
    EVENT_SOCKET_ERRORS_FOR_TOO_LONG,
    EVENT_TCP_SEND_TIMEOUT,
    // Generic log points for the user, do not change
    EVENT_USER_0,
    EVENT_USER_1,
    EVENT_USER_2,
    EVENT_USER_3,
    EVENT_USER_4,
    EVENT_USER_5,
    EVENT_USER_6,
    EVENT_USER_7,
    EVENT_USER_8,
    EVENT_USER_9,
    // Log points defined by the application, knock
    // yourself out, but do it in your own log_enum_app.h
    // file
#include "log_enum_app.h"
} LogEvent;

#ifdef __cplusplus
}
#endif

#endif

// End of file
