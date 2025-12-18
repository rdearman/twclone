#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
/* Start/stop the SysOp REPL on stdin/stdout. */
  void sysop_start (void);
  void sysop_stop (void);
/* (Optional) Directly dispatch a single line (for tests). */
  void sysop_dispatch_line (char *line);
#ifdef __cplusplus
}
#endif
