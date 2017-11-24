// The events as strings (must be kept in line with the
// LogEvent enum in log_enum.h).
// By convention, a "*" prefix means that a bad thing
// has happened, makes them easier to spot in a stream
// of prints flowing up the console window.
const char *gLogStrings[] = {
    // Log points required for all builds, do not change
    "  EMPTY",
    "  BUILD_TIME_UNIX_FORMAT",
    "  CURRENT_TIME_UTC",
    "  LOG_START",
    "  LOG_STOP",
    "  LOG_FILES_TO_UPLOAD",
    "  LOG_UPLOAD_STARTING",
    "  LOG_FILE_BYTE_COUNT",
    "  LOG_FILE_UPLOAD_COMPLETED",
    "  LOG_UPLOAD_TASK_COMPLETED",
    "  LOG_FILE_OPEN",
    "* LOG_FILE_OPEN_FAILURE",
    "  LOG_FILE_CLOSE",
    "  FILE_OPEN",
    "* FILE_OPEN_FAILURE",
    "  FILE_CLOSE",
    "  FILE_DELETED",
    "* FILE_DELETE_FAILURE",
    "  DIR_OPEN",
    "  DIR_OPEN_FAILURE",
    "  DIR_SIZE",
    "  DNS_LOOKUP",
    "* DNS_LOOKUP_FAILURE",
    "  SOCKET_OPENING",
    "* SOCKET_OPENING_FAILURE",
    "  SOCKET_OPENED",
    "  TCP_CONNECTING",
    "* TCP_CONNECT_FAILURE",
    "  TCP_CONNECTED",
    "  TCP_CONFIGURED",
    "* TCP_CONFIGURATION_FAILURE",
    "  SEND_START",
    "  SEND_STOP",
    "* SEND_FAILURE",
    "* SOCKET_GONE_BAD",
    "* SOCKET_ERRORS_FOR_TOO_LONG",
    "* TCP_SEND_TIMEOUT",
    // Generic log points for the user, do not change
    "  USER_0",
    "  USER_1",
    "  USER_2",
    "  USER_3",
    "  USER_4",
    "  USER_5",
    "  USER_6",
    "  USER_7",
    "  USER_8",
    "  USER_9",
    // Log points defined by the application, knock
    // yourself out, but do it in your own log_string_app.h
    // file
#include "log_strings_app.h"
};

const int gNumLogStrings = sizeof (gLogStrings) / sizeof (gLogStrings[0]);

// End of file
