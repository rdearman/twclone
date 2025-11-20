#ifndef SERVER_STARDOCK_H
#define SERVER_STARDOCK_H

#include <jansson.h>
#include "common.h" // For client_ctx_t

// Function prototypes for hardware-related commands
int cmd_hardware_list(client_ctx_t *ctx, json_t *root);
int cmd_hardware_buy(client_ctx_t *ctx, json_t *root);

#endif // SERVER_STARDOCK_H