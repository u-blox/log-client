/* mbed Microcontroller Library
 * Copyright (c) 2017 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "errno.h"
#include "log.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum length of a path (including trailing slash).
#define LOGGING_MAX_LEN_PATH 56

// The maximum length of a file name (including extension).
#define LOGGING_MAX_LEN_FILE_NAME 8

#define LOGGING_MAX_LEN_FILE_PATH (LOGGING_MAX_LEN_PATH + LOGGING_MAX_LEN_FILE_NAME)

// The maximum length of the URL of the logging server (including port).
#define LOGGING_MAX_LEN_SERVER_URL 128

// The TCP buffer size for log file uploads.
// Note: chose a small value here since the logs are small
// and it avoids a large malloc().
// Note: must be a multiple of a LogEntry size, otherwise
// the overhang can be lost
#define LOGGING_TCP_BUFFER_SIZE (20 * sizeof (LogEntry))

// Printf() logging data as well as putting it in the
// logging system
#if defined(MBED_CONF_APP_LOG_PRINT) && \
    MBED_CONF_APP_LOG_PRINT
#define LOG_PRINT
#endif

// Only printf() the logging data don't put it in the
// logging system at all (can sometimes be useful
// where you want local debug but don't want to
// load the rest of the system with logging data)
#if defined (MBED_CONF_APP_LOG_PRINT_ONLY) && \
    MBED_CONF_APP_LOG_PRINT_ONLY
#define LOG_PRINT_ONLY
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Type used to pass parameters to the log file upload callback.
typedef struct {
    FATFileSystem *pFileSystem;
    const char *pCurrentLogFile;
    NetworkInterface *pNetworkInterface;
} LogFileUploadData;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The strings associated with the enum values.
extern const char *gLogStrings[];
extern const int gNumLogStrings;

// A pointer to the logging context data.
// This is stored at the start of the logging
// buffer area
static LogContext *gpContext = NULL;

// Mutex to arbitrate logging.
// The callback which writes logging to disk
// will attempt to lock this mutex while the
// function that prints out the log owns the
// mutex. Note that the logging functions
// themselves shouldn't wait on it (they have
// no reason to as the buffering should
// handle any overlap); they MUST return quickly.
static Mutex gLogMutex;

// The number of calls to writeLog().
static int gNumWrites = 0;

// A logging timestamp.
static Timer gLogTime;

// Remember the last logging timestamp.
static unsigned int gLastLogTime;

// An offset in the logging timestamp (may be non-zero
// if logging has been suspended)
static unsigned int gLogTimeOffset;

// A file to write logs to.
static FILE *gpFile = NULL;

// The path where log files are kept.
static char gLogPath[LOGGING_MAX_LEN_PATH + 1];

// The name of the current log file.
static char gCurrentLogFileName[LOGGING_MAX_LEN_FILE_PATH + 1];

// The address of the logging server.
static SocketAddress *gpLoggingServer = NULL;

// A thread to run the log upload process.
static Thread *gpLogUploadThread = NULL;

// A buffer to hold some data that is required by the
// log file upload thread.
static LogFileUploadData *gpLogFileUploadData = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print a single item from a log.
void printLogItem(const LogEntry *pItem, unsigned int itemIndex)
{
    if (pItem->event > gNumLogStrings) {
        printf("%.3f: out of range event at entry %d (%d when max is %d)\n",
               (float) pItem->timestamp / 1000, itemIndex, pItem->event, gNumLogStrings);
    } else {
        printf ("%6.3f: %s [%d] %d (%#x)\n", (float) pItem->timestamp / 1000,
                gLogStrings[pItem->event], pItem->event, pItem->parameter, pItem->parameter);
    }

}

// Open a log file, storing its name in gCurrentLogFileName
// and returning a handle to it.
FILE *newLogFile()
{
    FILE *pFile = NULL;

    for (unsigned int x = 0; (x < 1000) && (pFile == NULL); x++) {
        sprintf(gCurrentLogFileName, "%s/%04d.log", gLogPath, x);
        // Try to open the file to see if it exists
        pFile = fopen(gCurrentLogFileName, "r");
        // If it doesn't exist, use it, otherwise close
        // it and go around again
        if (pFile == NULL) {
            printf("Log file will be \"%s\".\n", gCurrentLogFileName);
            pFile = fopen (gCurrentLogFileName, "wb+");
            if (pFile != NULL) {
                LOG(EVENT_LOG_FILE_OPEN, 0);
            } else {
                LOG(EVENT_LOG_FILE_OPEN_FAILURE, errno);
                perror ("Error initialising log file");
            }
        } else {
            fclose(pFile);
            pFile = NULL;
        }
    }

    return pFile;
}

// Get the address portion of a URL, leaving off the port number etc.
static void getAddressFromUrl(const char * pUrl, char * pAddressBuf, int lenBuf)
{
    const char * pPortPos;
    int lenUrl;

    if (lenBuf > 0) {
        // Check for the presence of a port number
        pPortPos = strchr(pUrl, ':');
        if (pPortPos != NULL) {
            // Length wanted is up to and including the ':'
            // (which will be overwritten with the terminator)
            if (lenBuf > pPortPos - pUrl + 1) {
                lenBuf = pPortPos - pUrl + 1;
            }
        } else {
            // No port number, take the whole thing
            // including the terminator
            lenUrl = strlen (pUrl);
            if (lenBuf > lenUrl + 1) {
                lenBuf = lenUrl + 1;
            }
        }
        memcpy (pAddressBuf, pUrl, lenBuf);
        *(pAddressBuf + lenBuf - 1) = 0;
    }
}

// Get the port number from the end of a URL.
static bool getPortFromUrl(const char * pUrl, int *port)
{
    bool success = false;
    const char * pPortPos = strchr(pUrl, ':');

    if (pPortPos != NULL) {
        *port = atoi(pPortPos + 1);
        success = true;
    }

    return success;
}

// Function to sit in a thread and upload log files.
void logFileUploadCallback()
{
    nsapi_error_t nsapiError;
    Dir *pDir = new Dir();
    int x;
    int y = 0;
    int z;
    struct dirent dirEnt;
    FILE *pFile = NULL;
    TCPSocket *pTcpSock = new TCPSocket();
    int sendCount;
    int sendTotalThisFile;
    int size;
    char *pReadBuffer = new char[LOGGING_TCP_BUFFER_SIZE];
    char fileNameBuffer[LOGGING_MAX_LEN_FILE_PATH];

    MBED_ASSERT (gpLogFileUploadData != NULL);

    LOG(EVENT_DIR_OPEN, 0);
    x = pDir->open(gpLogFileUploadData->pFileSystem, "/");
    if (x == 0) {
        // Send those log files, using a different TCP
        // connection for each one so that the logging server
        // stores them in separate files
        do {
            x = pDir->read(&dirEnt);
            // Open the file, provided it's not the one we're currently logging to
            if ((x == 1) && (dirEnt.d_type == DT_REG) &&
                ((gpLogFileUploadData->pCurrentLogFile == NULL) ||
                 (strcmp(dirEnt.d_name, gpLogFileUploadData->pCurrentLogFile) != 0))) {
                y++;
                LOG(EVENT_SOCKET_OPENING, y);
                nsapiError = pTcpSock->open(gpLogFileUploadData->pNetworkInterface);
                if (nsapiError == NSAPI_ERROR_OK) {
                    LOG(EVENT_SOCKET_OPENED, y);
                    pTcpSock->set_timeout(10000);
                    LOG(EVENT_TCP_CONNECTING, y);
                    nsapiError = pTcpSock->connect(*gpLoggingServer);
                    if (nsapiError == NSAPI_ERROR_OK) {
                        LOG(EVENT_TCP_CONNECTED, y);
                        LOG(EVENT_LOG_UPLOAD_STARTING, y);
                        sprintf(fileNameBuffer, "%s/%s", gLogPath, dirEnt.d_name);
                        pFile = fopen(fileNameBuffer, "r");
                        if (pFile != NULL) {
                            LOG(EVENT_LOG_FILE_OPEN, 0);
                            sendTotalThisFile = 0;
                            do {
                                // Read the file and send it
                                size = fread(pReadBuffer, 1, LOGGING_TCP_BUFFER_SIZE, pFile);
                                sendCount = 0;
                                while (sendCount < size) {
                                    z = pTcpSock->send(pReadBuffer + sendCount, size - sendCount);
                                    if (z > 0) {
                                        sendCount += z;
                                        sendTotalThisFile += z;
                                        LOG(EVENT_LOG_FILE_BYTE_COUNT, sendTotalThisFile);
                                    }
                                }
                            } while (size > 0);
                            LOG(EVENT_LOG_FILE_UPLOAD_COMPLETED, y);

                            // The file has now been sent, so close the socket
                            pTcpSock->close();

                            // If the upload succeeded, delete the file
                            if (feof(pFile)) {
                                if (remove(fileNameBuffer) == 0) {
                                    LOG(EVENT_FILE_DELETED, 0);
                                } else {
                                    LOG(EVENT_FILE_DELETE_FAILURE, 0);
                                }
                            }
                            LOG(EVENT_LOG_FILE_CLOSE, 0);
                            fclose(pFile);
                        } else {
                            LOG(EVENT_LOG_FILE_OPEN_FAILURE, 0);
                        }
                    } else {
                        LOG(EVENT_TCP_CONNECT_FAILURE, nsapiError);
                    }
                } else {
                    LOG(EVENT_SOCKET_OPENING_FAILURE, nsapiError);
                }
            }
        } while (x > 0);
    } else {
        LOG(EVENT_DIR_OPEN_FAILURE, x);
    }

    LOG(EVENT_LOG_UPLOAD_TASK_COMPLETED, 0);
    printf("[Log file upload background task has completed]\n");

    // Clear up locals
    delete pDir;
    delete pTcpSock;
    delete pReadBuffer;

    // Clear up globals
    delete gpLogFileUploadData;
    gpLogFileUploadData = NULL;
    delete gpLoggingServer;
    gpLoggingServer = NULL;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging.
void initLog(void *pBuffer)
{
    bool freshStart = false;

    gpContext = (LogContext *) pBuffer;
    // If the context is uninitialised, initialise it
    if ((gpContext->magicWord != 0x123456) ||
        (gpContext->version != LOG_VERSION)){
        freshStart = true;
        memset(gpContext, 0, sizeof(*gpContext));
        gpContext->version = LOG_VERSION;
        gpContext->pLog = (LogEntry * ) ((char *) pBuffer + sizeof(*gpContext));
        gpContext->pLogNextEmpty = gpContext->pLog;
        gpContext->pLogFirstFull = gpContext->pLog;
        gpContext->numLogItems = 0;
        gpContext->logEntriesOverwritten = 0;
        gpContext->magicWord = 0x123456;
    }
    gLastLogTime = 0;
    gLogTime.reset();
    gLogTime.start();
    gLogTimeOffset = 0;
    if (freshStart) {
        LOG(EVENT_LOG_START, LOG_VERSION);
    } else {
        LOG(EVENT_LOG_START_AGAIN, LOG_VERSION);
    }
}

// Suspend logging.
void suspendLog()
{
    gLogTime.stop();
}

// Resume logging.
void resumeLog(unsigned int intervalUSeconds)
{
    gLogTimeOffset += intervalUSeconds;
    gLogTime.start();
}

// Get the first N log entries.
int getLog(LogEntry *pEntries, int numEntries)
{
    const LogEntry *pItem;
    int itemCount;

    gLogMutex.lock();

    itemCount = 0;
    pItem = gpContext->pLogFirstFull;
    while ((pItem != gpContext->pLogNextEmpty) &&
           (itemCount < numEntries)) {
        if (gpContext->logEntriesOverwritten > 0) {
            LogEntry insert = {pItem->timestamp,
                               EVENT_LOG_ENTRIES_OVERWRITTEN,
                               (int) gpContext->logEntriesOverwritten};
            memcpy(pEntries, &insert, sizeof(*pEntries));
            itemCount++;
            pEntries++;
            gpContext->logEntriesOverwritten = 0;
        }
        if (itemCount < numEntries) {
            memcpy(pEntries, pItem, sizeof(*pEntries));
            itemCount++;
            pEntries++;
            pItem++;
            if (gpContext->numLogItems > 0) {
                gpContext->numLogItems--;
            }
            if (pItem >= gpContext->pLog + MAX_NUM_LOG_ENTRIES) {
                pItem = gpContext->pLog;
            }
            gpContext->pLogFirstFull = pItem;
        }
    }

    gLogMutex.unlock();

    return itemCount;
}

// Get the number of log entries.
int getNumLogEntries()
{
    return gpContext->numLogItems;
}

// Initialise the log file.
bool initLogFile(const char *pPath)
{
    bool goodPath = true;
    int x;

    // Save the path
    if (pPath == NULL) {
        gLogPath[0] = 0;
    } else {
        if (strlen(pPath) < sizeof (gCurrentLogFileName) - LOGGING_MAX_LEN_FILE_NAME) {
            strcpy(gLogPath, pPath);
            x = strlen(gLogPath);
            // Remove any trailing slash
            if (gLogPath[x - 1] == '/') {
                gLogPath[x - 1] = 0;
            }
        } else {
            goodPath = false;
        }
    }

    if (goodPath) {
        gpFile = newLogFile();
    }

    return (gpFile != NULL);
}

// Upload previous log files.
bool beginLogFileUpload(FATFileSystem *pFileSystem,
                        NetworkInterface *pNetworkInterface,
                        const char *pLoggingServerUrl)
{
    bool success = false;
    char *pBuf = new char[LOGGING_MAX_LEN_SERVER_URL];
    Dir *pDir = new Dir();
    int port;
    int x;
    int y;
    int z = 0;
    struct dirent dirEnt;
    const char * pCurrentLogFile = NULL;

    if (gpLogUploadThread == NULL) {
        // First, determine if there are any log files to be uploaded.
        LOG(EVENT_DIR_OPEN, 0);
        // Note sure I understand why but you can't use the
        // partition path, which is *required* when opening a
        // file, when you are opening the root directory of
        // the partition
        x = pDir->open(pFileSystem, "/");
        if (x == 0) {
            printf("[Checking for log files to upload...]\n");
            // Point to the name portion of the current log file
            // (format "*/xxxx.log")
            pCurrentLogFile = strstr(gCurrentLogFileName, ".log");
            if (pCurrentLogFile != NULL) {
                pCurrentLogFile -= 4; // Point to the start of the file name
            }
            do {
                y = pDir->read(&dirEnt);
                if ((y == 1) && (dirEnt.d_type == DT_REG) &&
                    ((pCurrentLogFile == NULL) || (strcmp(dirEnt.d_name, pCurrentLogFile) != 0))) {
                    z++;
                }
            } while (y > 0);

            LOG(EVENT_LOG_FILES_TO_UPLOAD, z);
            printf("[%d log file(s) to upload]\n", z);

            if (z > 0) {
                gpLoggingServer = new SocketAddress();
                getAddressFromUrl(pLoggingServerUrl, pBuf, LOGGING_MAX_LEN_SERVER_URL);
                LOG(EVENT_DNS_LOOKUP, 0);
                printf("[Looking for logging server URL \"%s\"...]\n", pBuf);
                if (pNetworkInterface->gethostbyname(pBuf, gpLoggingServer) == 0) {
                    printf("[Found it at IP address %s]\n", gpLoggingServer->get_ip_address());
                    if (getPortFromUrl(pLoggingServerUrl, &port)) {
                        gpLoggingServer->set_port(port);
                        printf("[Logging server port set to %d]\n", gpLoggingServer->get_port());
                    } else {
                        printf("[WARNING: no port number was specified in the logging server URL (\"%s\")]\n",
                                pLoggingServerUrl);
                    }
                } else {
                    LOG(EVENT_DNS_LOOKUP_FAILURE, 0);
                    printf("[Unable to locate logging server \"%s\"]\n", pLoggingServerUrl);
                }

                gpLogUploadThread = new Thread();
                if (gpLogUploadThread != NULL) {
                    // Note: this will be destroyed by the log file upload thread when it finishes
                    gpLogFileUploadData = new LogFileUploadData();
                    gpLogFileUploadData->pCurrentLogFile = pCurrentLogFile;
                    gpLogFileUploadData->pFileSystem = pFileSystem;
                    gpLogFileUploadData->pNetworkInterface = pNetworkInterface;
                    if (gpLogUploadThread->start(callback(logFileUploadCallback)) == osOK) {
                        printf("[Log file upload background task is now running]\n");
                        success = true;
                    } else {
                        delete gpLogFileUploadData;
                        gpLogFileUploadData = NULL;
                        printf("[Unable to start thread to upload files to logging server]\n");
                    }
                } else {
                    printf("[Unable to instantiate thread to upload files to logging server]\n");
                }
            } else {
                success = true; // Nothing to do
            }
        } else {
            LOG(EVENT_DIR_OPEN_FAILURE, x);
            printf("[Unable to open path \"%s\" (error %d)]\n", gLogPath, x);
        }
        delete pDir;
    } else {
        printf("[Log file upload task already running]\n");
    }
    delete pBuf;

    return success;
}

// Stop uploading previous log files, returning memory.
void stopLogFileUpload()
{
    if (gpLogUploadThread != NULL) {
        gpLogUploadThread->terminate();
        gpLogUploadThread->join();
        delete gpLogUploadThread;
        gpLogUploadThread = NULL;
    }

    if (gpLogFileUploadData != NULL) {
        delete gpLogFileUploadData;
        gpLogFileUploadData = NULL;
    }

    if (gpLoggingServer != NULL) {
        delete gpLoggingServer;
        gpLoggingServer = NULL;
    }
}

// Log an event plus parameter.
// Note: ideally we'd mutex in here but I don't
// want any overheads or any cause for delay
// so please just cope with any very occasional
// logging corruption which may occur
void LOG(LogEvent event, int parameter)
{
    unsigned int timeStamp = ((unsigned int) gLogTime.read_us()) + gLogTimeOffset;

    if (gpContext->pLogNextEmpty) {
        // Check if the timestamp has wrapped and
        // insert a log point before this one if that's the
        // case (coding gods: please excuse my recursion)
        if (timeStamp < gLastLogTime) {
            gLastLogTime = timeStamp;
            LOG(EVENT_LOG_TIME_WRAP, timeStamp);
        }
        gLastLogTime = timeStamp;
        gpContext->pLogNextEmpty->timestamp = timeStamp;
        gpContext->pLogNextEmpty->event = (int) event;
        gpContext->pLogNextEmpty->parameter = parameter;
#if defined(LOG_PRINT) || defined(LOG_PRINT_ONLY)
        printLogItem(gpContext->pLogNextEmpty, 0);
#endif
#ifndef LOG_PRINT_ONLY
        if (gpContext->pLogNextEmpty < gpContext->pLog + MAX_NUM_LOG_ENTRIES - 1) {
            gpContext->pLogNextEmpty++;
        } else {
            gpContext->pLogNextEmpty = gpContext->pLog;
        }

        if (gpContext->pLogNextEmpty == gpContext->pLogFirstFull) {
            // Logging has wrapped, so move the
            // first pointer on to reflect the
            // overwrite
            if (gpContext->pLogFirstFull < gpContext->pLog + MAX_NUM_LOG_ENTRIES - 1) {
                gpContext->pLogFirstFull++;
            } else {
                gpContext->pLogFirstFull = gpContext->pLog;
            }
            gpContext->logEntriesOverwritten++;
        } else {
            gpContext->numLogItems++;
        }
#endif
    }
}

// Log an event plus parameter, this time with mutex.
// Note: use this version if you don't care about speed
// so much
void LOGX(LogEvent event, int parameter)
{
    unsigned int timeStamp;

    gLogMutex.lock();
    timeStamp = ((unsigned int) gLogTime.read_us()) + gLogTimeOffset;

    if (gpContext->pLogNextEmpty) {
        // Check if the timestamp has wrapped and
        // insert a log point before this one if that's the
        // case
        if (timeStamp < gLastLogTime) {
            gLastLogTime = timeStamp;
            LOG(EVENT_LOG_TIME_WRAP, timeStamp);
        }
        gLastLogTime = timeStamp;
        gpContext->pLogNextEmpty->timestamp = timeStamp;
        gpContext->pLogNextEmpty->event = (int) event;
        gpContext->pLogNextEmpty->parameter = parameter;
#if defined(LOG_PRINT) || defined(LOG_PRINT_ONLY)
        printLogItem(gpContext->pLogNextEmpty, 0);
#endif
#ifndef LOG_PRINT_ONLY
        if (gpContext->pLogNextEmpty < gpContext->pLog + MAX_NUM_LOG_ENTRIES - 1) {
            gpContext->pLogNextEmpty++;
        } else {
            gpContext->pLogNextEmpty = gpContext->pLog;
        }

        if (gpContext->pLogNextEmpty == gpContext->pLogFirstFull) {
            // Logging has wrapped, so move the
            // first pointer on to reflect the
            // overwrite
            if (gpContext->pLogFirstFull < gpContext->pLog + MAX_NUM_LOG_ENTRIES - 1) {
                gpContext->pLogFirstFull++;
            } else {
                gpContext->pLogFirstFull = gpContext->pLog;
            }
            gpContext->logEntriesOverwritten++;
        } else {
            gpContext->numLogItems++;
        }
#endif
    }

    gLogMutex.unlock();
}

// Flush the log file.
// Note: log file mutex must be locked before calling.
void flushLog()
{
    if (gpFile != NULL) {
        fclose(gpFile);
        gpFile = fopen(gCurrentLogFileName, "ab+");
    }
}

// This should be called periodically to write the log
// to file, if a filename was provided to initLog().
void writeLog()
{
    if (gLogMutex.trylock()) {
        if (gpFile != NULL) {
            gNumWrites++;
            while (gpContext->pLogNextEmpty != gpContext->pLogFirstFull) {
                if (gpContext->logEntriesOverwritten > 0) {
                    LogEntry insert = {gpContext->pLogFirstFull->timestamp,
                                       EVENT_LOG_ENTRIES_OVERWRITTEN,
                                       (int) gpContext->logEntriesOverwritten};
                    fwrite(&insert, sizeof(insert), 1, gpFile);
                    gpContext->logEntriesOverwritten = 0;
                }
                fwrite(gpContext->pLogFirstFull, sizeof(LogEntry), 1, gpFile);
                if (gpContext->pLogFirstFull < gpContext->pLog + MAX_NUM_LOG_ENTRIES - 1) {
                    gpContext->pLogFirstFull++;
                } else {
                    gpContext->pLogFirstFull = gpContext->pLog;
                }
                if (gpContext->numLogItems > 0) {
                    gpContext->numLogItems--;
                }
            }
            if (gNumWrites > LOGGING_NUM_WRITES_BEFORE_FLUSH) {
                gNumWrites = 0;
                flushLog();
            }
        }
        gLogMutex.unlock();
    }
}

// Close down logging.
void deinitLog()
{
    stopLogFileUpload(); // Just in case

    LOG(EVENT_LOG_STOP, LOG_VERSION);
    if (gpFile != NULL) {
        writeLog();
        flushLog(); // Just in case
        LOG(EVENT_LOG_FILE_CLOSE, 0);
        fclose(gpFile);
        gpFile = NULL;
    }

    gLogTime.stop();

    // Don't reset the variables
    // here so that printLog() still
    // works afterwards if we're just
    // logging to RAM rather than
    // to file.
}

// Print out the log.
void printLog()
{
    const LogEntry *pItem = gpContext->pLogNextEmpty;
    LogEntry fileItem;
    bool loggingToFile = false;
    FILE *pFile = gpFile;
    unsigned int x = 0;

    gLogMutex.lock();
    printf ("------------- Log starts -------------\n");
    if (pFile != NULL) {
        // If we were logging to file, read it back
        // First need to flush the file to disk
        loggingToFile = true;
        fclose(gpFile);
        gpFile = NULL;
        LOG(EVENT_LOG_FILE_CLOSE, 0);
        pFile = fopen(gCurrentLogFileName, "rb");
        if (pFile != NULL) {
            LOG(EVENT_LOG_FILE_OPEN, 0);
            while (fread(&fileItem, sizeof(fileItem), 1, pFile) == 1) {
                printLogItem(&fileItem, x);
                x++;
            }
            // If we're not at the end of the file, there must have been an error
            if (!feof(pFile)) {
                perror ("Error reading portion of log stored in file system");
            }
            fclose(pFile);
            LOG(EVENT_LOG_FILE_CLOSE, 0);
        } else {
            perror ("Error opening portion of log stored in file system");
        }
    }

    // Print the log items remaining in RAM
    pItem = gpContext->pLogFirstFull;
    while (pItem != gpContext->pLogNextEmpty) {
        printLogItem(pItem, x);
        x++;
        pItem++;
        if (pItem >= gpContext->pLog + MAX_NUM_LOG_ENTRIES) {
            pItem = gpContext->pLog;
        }
    }

    // Allow writeLog() to resume with the same file name
    if (loggingToFile) {
        gpFile = fopen(gCurrentLogFileName, "ab+");
        if (gpFile) {
            LOG(EVENT_LOG_FILE_OPEN, 0);
        } else {
            LOG(EVENT_LOG_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    }

    printf ("-------------- Log ends --------------\n");
    gLogMutex.unlock();
}

// End of file
