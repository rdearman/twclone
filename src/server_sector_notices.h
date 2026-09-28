#ifndef SERVER_SECTOR_NOTICES_H
#define SERVER_SECTOR_NOTICES_H

#include "common.h"
#include <jansson.h>

int server_sector_notice_publish (client_ctx_t *ctx, json_t *root,
                                  int sector_id, const char *subtype,
                                  json_t *details);

#endif
