/* boxmuller.c           Implements the Polar form of the Box-Muller
                         Transformation

                      (c) Copyright 1994, Everett F. Carter Jr.
                          Permission is granted by the author to use
                          this software for any application provided this
                          copyright notice is preserved.

*/

#ifdef HAVE_CONFIG_H
#  include <autoconf.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_MATH_H
#  include <math.h>
#endif /* HAVE_MATH_H */


//extern float ranf();         /* ranf() is uniform in 0..1 */

//Using rand()/RAND_MAX instead of ranf

float
box_muller (float m, float s)	/* normal random variate generator */
{				/* mean m, standard deviation s */
  float x1, x2, w, y1;
  static float y2;
  static int use_last = 0;

  if (use_last)			/* use value from previous call */
    {
      y1 = y2;
      use_last = 0;
    }
  else
    {
      do
	{
	  x1 = 2.0 * ((float) rand () / RAND_MAX) - 1.0;
	  x2 = 2.0 * ((float) rand () / RAND_MAX) - 1.0;
	  w = x1 * x1 + x2 * x2;
	}
      while (w >= 1.0);

      w = sqrt ((-2.0 * log (w)) / w);
      y1 = x1 * w;
      y2 = x2 * w;
      use_last = 1;
    }

  return (m + y1 * s);
}
