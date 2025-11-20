#ifndef SERVER_STARDOCK_H
#define SERVER_STARDOCK_H

#include <jansson.h>
#include "common.h" // For client_ctx_t

// Location Types
#define LOCATION_STARDOCK "STARDOCK"
#define LOCATION_CLASS0 "CLASS0"
#define LOCATION_OTHER "OTHER"

// Port Types
#define PORT_TYPE_STARDOCK 9
#define PORT_TYPE_CLASS0 0

// Hardware Categories
#define HW_CATEGORY_FIGHTER "FIGHTER"
#define HW_CATEGORY_SHIELD "SHIELD"
#define HW_CATEGORY_HOLD "HOLD"
#define HW_CATEGORY_SPECIAL "SPECIAL"
#define HW_CATEGORY_MODULE "MODULE"

// Specific Hardware Item Codes
#define HW_ITEM_GENESIS "GENESIS"
#define HW_ITEM_DETONATOR "DETONATOR"
#define HW_ITEM_PROBE "PROBE"
#define HW_ITEM_CLOAK "CLOAK"
#define HW_ITEM_TWARP "TWARP"
#define HW_ITEM_PSCANNER "PSCANNER"
#define HW_ITEM_LSCANNER "LSCANNER"

// General Hardware Constants
#define HW_MAX_PER_SHIP_DEFAULT -1
#define HW_MIN_QUANTITY 1

// Helper for stringification of macros
#define QUOTE(name) #name

// Function prototypes for hardware-related commands
int cmd_hardware_list(client_ctx_t *ctx, json_t *root);
int cmd_hardware_buy(client_ctx_t *ctx, json_t *root);

#endif // SERVER_STARDOCK_H