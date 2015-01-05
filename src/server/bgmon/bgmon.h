/*
 * Protocol
 *
 * All multi-byte values are big-endian. The server will respond to any request
 * with a response of the same type or a response of type TYPE_ERR.
 *
 * Header:
 * two bytes: size of message including header
 * two bytes: type of message (TYPE_)
 *
 * TYPE_ERR (request and response):
 * two bytes: type of error (ERR_)
 *
 * TYPE_LIST (request):
 * none
 *
 * TYPE_LIST (response):
 * two bytes: number of processes
 * for each process:
 *   two bytes: process number
 *   two bytes: x = length of name
 *   x bytes: name of process
 *
 * TYPE_START (request):
 * two bytes: x = length of name
 * x bytes: name of process
 *
 * TYPE_START (response):
 * two bytes: process number
 *
 * TYPE_KILL (request):
 * two bytes: process number
 *
 * TYPE_KILL (response):
 * none
 *
 */

#define TYPE_ERR 0
#define TYPE_LIST 1
#define TYPE_START 2
#define TYPE_KILL 3
#define TYPE_SUSPEND 4
#define TYPE_RESUME 5
#define TYPE_KILLALL 6
#define TYPE_SUSPENDALL 7
#define TYPE_RESUMEALL 8

#define ERR_INVAL 0
#define ERR_NOMEM 1
#define ERR_KILL 2
