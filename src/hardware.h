#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <jansson.h>

// A structure to define hardware items
struct hardware
{
  int id;
  const char *name;
  int price;
  int size;
};

// Function prototypes for the hardware-related functions
// json_t *list_hardware (void);
json_t *list_hardware (json_t * json_data, struct player *curplayer);
json_t *buy_hardware (json_t * json_data, struct player *curplayer);


#endif // _HARDWARE_H_
