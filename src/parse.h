/*
Copyright (C) 2000 Jason C. Garcowski(jcg5@po.cwru.edu), 
                   Ryan Glasnapp(rglasnap@nmt.edu)

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

#ifndef PARSE_H
#define PARSE_H

/*
  addstring

  MAKE SURE YOU HAVE THE MEMORY ALLOCATED FOR THE STRINGS!

  If strlen(list) == 0, then the first character of list is a delimiter
  Then all of item is copied to list, and then another delimiter is added
*/

void addstring (char *list, char *item, char delimiter, int maxlistsize);

/*
  Works the same as above.
*/
void addint (char *list, int num, char delimiter, int maxlistsize);

/*
  popstring

  MAKE SURE YOU HAVE THE MEMORY ALLOCATED FOR THE STRINGS!

  If the first character in list is in delimiters, it is removed.
  All characters from that point up until the next delimiter are 
  copied into item.  Upon returning, list will have everything in 
  item, and the delimiter removed
*/
void popstring (char *list, char *item, char *delimiters, int maxitemsize);

/*
  works the same way as popstring
*/
int popint (char *list, char *delimiters);

#endif
