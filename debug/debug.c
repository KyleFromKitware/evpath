#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#include "debug.h"

#ifdef DEBUG

/* print message in arglist */
void _vwarn(const char* file, int line, const char* s, va_list ap)
{
        fprintf(stderr, "(%s,%d): ",
                        file, line);
        vfprintf(stderr,s,ap);
		fprintf(stderr, "\n");
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
    vfprintf(stderr,s,ap);
	fprintf(stderr, "\n");
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