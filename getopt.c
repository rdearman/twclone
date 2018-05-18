/* -*- C -*-
   $Id: getopt.c,v 1.2 2002/07/14 00:00:25 npsimons Exp $
   Copyright (C) 2002, Nathan Paul Simons (npsimons@hardcorehackers.com)
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or 
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of 
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License 
   along with this program; if not, write to the Free Software 
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
   
   Alternatively, the GPL can be found at 
   http://www.gnu.org/copyleft/gpl.html */

#include <stdlib.h>
#include <string.h>

char *optarg;
int optind = 0, opterr, optopt;

/** Re-implementation of getopt from scratch.
    
    This is a re-implementation of getopt(3) from the GNU standard C
    library.  It is re-created here in order to maintain portability.
    See the man page for getopt(3) for more details.

    @author Nathan Paul Simons <SimonsNP@navair.navy.mil>
    @version $Revision: 1.2 $
    @return The option character if the option was found successfully,
    ':' if there was a missing parameter for one of the
    options, '?' for an unknown option character, or -1 for
    the end of the option list.
    @param argc Argument count as passed to the main() function on
    program invocation.
    @param argv Argument array as passed to the main() function on
    program invocation.
    @param optstring A string containing the legitimate option
    characters.
*/
int
getopt (int argc, char *const argv[], const char *opstring)
{
  unsigned int i = 0;
  char optchar = '\0';

  /* Increase our index in the list of arguments. */
  optind++;

  if (optind >= argc)
    return (-1);

  /* This argument is not an option for us to parse; stop parsing. */
  if (argv[optind][0] != '-')
    return (-1);

  optchar = argv[optind][1];

  /* Go through the option string to test if this is a recognised
     option. */
  for (i = 0; i < strlen (opstring); i++)
    {
      if (opstring[i] == optchar)
	{
	  /* If this option takes an argument . . . */
	  if ((opstring[i + 1] != '\0') && (opstring[i + 1] == ':'))
	    {
	      /* If we are missing the argument. */
	      if ((optind + 1 < argc) && (argv[optind + 1][0] != '-'))
		{
		  optarg = argv[optind + 1];
		  optind++;
		  return ((int) optchar);
		}
	      optopt = optchar;
	      return ((int) ':');
	    }
	  return ((int) optchar);
	}
    }

  optopt = optchar;
  return ((int) '?');
}
