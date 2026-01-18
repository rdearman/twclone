#ifndef SERVER_BULK_H
#define SERVER_BULK_H
#include <jansson.h>
#include "common.h"
int cmd_bulk_execute (client_ctx_t * ctx, json_t * root);
#endif
