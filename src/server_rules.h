#ifndef SERVER_RULES_H
#define SERVER_RULES_H
#include "server_envelope.h"
#include "common.h"

// #define STUB_NIY(_ctx,_root,_cmd) \
//   do { \
//     (void)(_cmd); \
//     send_enveloped_error((_ctx)->fd, (_root), 1101, "Not implemented"); \
//     return 0; \
//   } while (0)

#define STUB_NIY(_ctx,_root,_cmd) \ 
  do { \
    (void)(_cmd); \
    send_enveloped_error(((client_ctx_t*)(_ctx))->fd, \
                         (_root), \
                         1101, \ 
                         "Not implemented: " #_cmd); /* Better debug message */ \ 
    return 0; \ 
  } while (0)

#endif
