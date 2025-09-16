/*
Copyright (C) 2000 Ryan Glasnapp(rglasnap@nmt.edu),
                   Rick Dearman(rick@ricken.demon.co.uk)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/* Modification History **
**************************
** 
** LAST MODIFICATION DATE: 11 June 2002
** Mod. Author: Rick Dearman
**
** 1) Modified all comments from // to C comments in case a users complier isn't C99 
**    compliant. (like some older Sun or HP compilers)
**
** 2) Added owner to insert planet
** 
*/



#ifndef PLANET_H_
#define PLANET_H_

#include "universe.h"

#ifndef RAND_MAX
#define RAND_MAX 1
#endif

#define MAX_SAFE_PLANETS 5
planetClass **planetTypes;
struct planet **planets;

int init_planets (char *filename);
void saveplanets(char *filename);
void init_planetinfo(char *filename);
void save_planetinfo(char *filename);
int insert_planet (struct planet *p, struct sector *s, int playernumber);

#endif
