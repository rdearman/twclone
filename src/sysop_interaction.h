#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
#include <signal.h>

/* Start/stop the SysOp REPL on stdin/stdout. */
void sysop_start (volatile sig_atomic_t *running_flag);
void sysop_stop (void);
/* (Optional) Directly dispatch a single line (for tests). */
void sysop_dispatch_line (char *line);
#ifdef __cplusplus
}
#endif
