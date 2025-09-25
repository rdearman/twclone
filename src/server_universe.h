#ifndef SERVER_UNIVERSE_H
#define SERVER_UNIVERSE_H

#include "config.h"

/* Insert default config values into DB if missing */
int initconfig(void);

/* Initialise universe (check DB, run bigbang if needed) */
int universe_init(void);

/* Shutdown universe (cleanup hooks if needed) */
void universe_shutdown(void);

#endif /* SERVER_UNIVERSE_H */
