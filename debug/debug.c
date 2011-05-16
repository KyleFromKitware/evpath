#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>

#include "debug.h"

#ifdef DEBUG

/* print message in arglist */
void _vwarn(const char* file, int line, const char* s, va_list ap)
{
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        fprintf(stderr, "[%ld,%ld](%s,%d): ",
                        time.tv_sec, time.tv_nsec, file, line);
        vfprintf(stderr,s,ap);
}

/* print message in varargs */
void _warn(const char* file, int line, const char *s, ...)
{
        va_list ap;
        va_start(ap,s);
        _vwarn(file,line,s,ap);
        va_end(ap);
}

/* print message in varargs and exit with error code */
void _error(const char* file, int line, int code, const char *s, ...)
{
        va_list ap;
        va_start(ap,s);
        _vwarn(file,line,s,ap);
        va_end(ap);
        exit(code);
}

#else /* DEBUG */

/* print message in arglist */
void _vwarn(const char *s, va_list ap)
{
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        fprintf(stderr, "[%ld,%ld]: ",
                        time.tv_sec, time.tv_nsec);
        vfprintf(stderr,s,ap);
}

/* print message in varargs */
void _warn(const char *s, ...)
{
        va_list ap;
        va_start(ap,s);
        _vwarn(s,ap);
        va_end(ap);
}

/* print message in varargs and exit with error code */
void _error(int code, const char *s, ...)
{
        va_list ap;
        va_start(ap,s);
        _vwarn(s,ap);
        va_end(ap);
        exit(code);
}

#endif /* DEBUG */