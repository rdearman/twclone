#ifndef SERVER_LOG_H
#define SERVER_LOG_H
#include <stdarg.h>
#include <syslog.h>
#ifdef __cplusplus
extern "C"
{
#endif
/* ---- FILE BACKEND ----
 * Write logs to a plain file (no syslog).
 * echo_stderr: 0 = silent to console; 1 = also mirror to stderr
 * max_level: LOG_DEBUG/LOG_INFO/LOG_NOTICE/LOG_WARNING/LOG_ERR/...
 */
void server_log_init_file (const char *filepath,
                           const char *prefix,
                           int echo_stderr, int max_level);
/* Reopen the file (call on SIGHUP for rotation). No-op if not file-backed. */
void server_log_reopen (void);
/* (Optional) Syslog backend, if you ever want it back. */
void server_log_init (const char *prog_tag,
                      const char *prefix,
                      int facility, int echo_stderr, int max_level);
/* Set max level at runtime (works for both backends). */
void server_log_set_level (int max_level);
/* Change the message prefix at runtime (e.g., "[server]" / "[engine]"). */
void server_log_set_prefix (const char *prefix);
/* Close log resources (optional). */
void server_log_close (void);
/* Core + convenience */
void server_log_printf (int priority, const char *fmt, ...)
__attribute__((format (printf, 2, 3)));
void server_log_vprintf (int priority, const char *fmt, va_list ap);
#define LOGE(fmt, ...) server_log_printf (LOG_ERR,     fmt, ## __VA_ARGS__)
#define LOGW(fmt, ...) server_log_printf (LOG_WARNING, fmt, ## __VA_ARGS__)
#define LOGN(fmt, ...) server_log_printf (LOG_NOTICE,  fmt, ## __VA_ARGS__)
#define LOGI(fmt, ...) server_log_printf (LOG_INFO,    fmt, ## __VA_ARGS__)
#define LOGD(fmt, ...) server_log_printf (LOG_DEBUG,   fmt, ## __VA_ARGS__)
#ifdef __cplusplus
}
#endif
#endif
