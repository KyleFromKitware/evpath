#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG

#define WARN(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define ERROR(...) _error(__FILE__, __LINE__, __VA_ARGS__)
#define INFO(...) WARN(__VA_ARGS__)

/* print message in arglist */
void _vwarn(const char* file, int line, const char* s, va_list ap);

/* print message in varargs */
void _warn(const char* file, int line, const char *s, ...);

/* print message in varargs and exit with ERROR code */
void _error(const char* file, int line, int code, const char *s, ...) __attribute__ ((noreturn));

#else /* DEBUG */

#define WARN(...) _warn(__VA_ARGS__)
#define ERROR(...) _error(__VA_ARGS__)
#define INFO(...)

/* print message in arglist */
void _vwarn(const char *s, va_list ap);

/* print message in varargs */
void _warn(const char *s, ...);

/* print message in varargs and exit with ERROR code */
void _error( int code, const char* s, ...) __attribute__ ((noreturn));

#endif /* DEBUG */
#endif