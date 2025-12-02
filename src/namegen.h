#ifndef NAMEGEN_H
#define NAMEGEN_H
#include <stdlib.h>
extern int flip;
// Function prototypes
char *randomname (char *name);
char *consellationName (char *name);
void init_usedNames (void);
// External array declarations
extern char *nameCollection[];
extern char *firstSyllable[];
extern char *middleSyllable[];
extern char *lastSyllable[];
#endif
