#ifndef FREEHTTPD_LOG_H
#define FREEHTTPD_LOG_H

#ifndef NO_COLORS
#    define COLOR(c, s) "\033[" c "m" s "\033[0m"
#else
#    define COLOR(c, s) s
#endif

#define LOG_DEBUG COLOR ("2", "debug: ")
#define LOG_INFO COLOR ("1;34", "info: ")
#define LOG_WARN COLOR ("1;33", "warning: ")
#define LOG_ERR COLOR ("1;31", "error: ")

void __attribute__ ((format (printf, 1, 2))) log_msg (const char *fmt, ...);
void __attribute__ ((format (printf, 1, 2))) log_err (const char *fmt, ...);

#endif /* FREEHTTPD_LOG_H */