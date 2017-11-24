Introduction
============
This class provides a simple, fast, binary logging facility written in C (not C++) so that it can be used anywhere in an Mbed application.

Each log entry contains three things:

1.  A microsecond-accurate timestamp (32 bits).
2.  The logging event that occurred (32 bits).
3.  A 32 bit parameter carrying further information about the logging event.

Functions are provided to print out the log to a terminal and, where a file system is available, to store the log to file and, where a sockets interface is avaialble, to send stored logs off to a logging server.

Usage
=====
The pattern of usage is as follows:

1. Create a pair of files:

   - `log_enum_app.h`
   - `log_strings_app.h`

   In these files add the log events that you require for your application (see the
   template files for an example of the format).

2. Place calls to `LOG()` anywhere in your code where you wish to log an event.
   For instance, if you have defined a log event `EVENT_BATTERY_VOLTAGE` then
   you could log the event with the battery voltage as the parameter as follows: 

   `LOG(EVENT_BATTERY_VOLTAGE, voltage);`

   By convention, if no parameter is required for a log item then 0 is used.

3. Near the start of your code, add a call to `initLog()`, passing in a pointer to a
   logging buffer of size `LOG_STORE_SIZE` bytes; logging will begin at this point.

4. If a file system is available:

   4.1 Call `initLogFile()` and pass in the path at which log files can be stored.
       Log files will be created with unique names (`xxxx.log`, where `xxxx` is a number
       between `0` and `9999`).

   4.2 Periodically call `writeLog()` so that the logged data can be written away to file.

5. If a network interface is available as well as a file system:

   5.1 At startup, call `beginLogFileUpload()`.  This will check for any stored log
       files and upload them to the given server URL in a separate thread.

   5.2 At the server URL there must be a logging server application, an example of which
       (written in Golang) can be found at https://github.com/u-blox/ioc-log, which
       receives and stores the logs.

6. When logging is to be stopped, call `deinitLog()` (and potentially before that
   `stoplogFileUpload()` in case a log file upload was still in progress).

7. To print out the logging data that has been captured since `initLog()` to the console,
   call `printLog()`. Note that if no file system is available only the logging data
   that could be stored in the `LOG_STORE_SIZE` buffer will be printed.

Note: there is no semaphore protection on the `LOG()` call since the priority is to log quickly and efficiently.  Hence it is possible for two `LOG()` calls to collide resulting in those particular log calls being mangled.  This will happen very rarely (I've never seen it happen in fact) but be aware that it is a possibility.
