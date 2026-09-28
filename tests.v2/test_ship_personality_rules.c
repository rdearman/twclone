#include <assert.h>
#include <stddef.h>
#include "ship_personality.h"

int
main (void)
{
  assert (ship_personality_parse (NULL) == SHIP_PERSONALITY_BALANCED);
  assert (ship_personality_parse ("OFFENSIVE") == SHIP_PERSONALITY_OFFENSIVE);
  assert (ship_personality_parse ("defensive") == SHIP_PERSONALITY_DEFENSIVE);
  assert (ship_personality_parse ("looter") == SHIP_PERSONALITY_LOOTER);
  assert (ship_personality_parse ("blockader") == SHIP_PERSONALITY_BLOCKADER);
  assert (ship_personality_parse ("unknown") == SHIP_PERSONALITY_BALANCED);
  assert (ship_personality_resolve ("offensive", "looter", "blockader")
          == SHIP_PERSONALITY_OFFENSIVE);
  assert (ship_personality_resolve (NULL, "defensive", "looter")
          == SHIP_PERSONALITY_DEFENSIVE);
  assert (ship_personality_resolve (NULL, NULL, "blockader")
          == SHIP_PERSONALITY_BLOCKADER);
  assert (ship_personality_resolve (NULL, NULL, NULL)
          == SHIP_PERSONALITY_BALANCED);

  assert (ship_personality_target (SHIP_PERSONALITY_BALANCED, 20, 50, 70,
                                  80, 21, 90) == 70);
  assert (ship_personality_target (SHIP_PERSONALITY_OFFENSIVE, 20, 50, 70,
                                  80, 21, 90) == 80);
  assert (ship_personality_target (SHIP_PERSONALITY_OFFENSIVE, 20, 50, 70,
                                  10, 21, 90) == 20);
  assert (ship_personality_target (SHIP_PERSONALITY_DEFENSIVE, 20, 50, 70,
                                  80, 21, 90) == 21);
  assert (ship_personality_target (SHIP_PERSONALITY_DEFENSIVE, 20, 50, 70,
                                  80, 0, 90) == 20);
  assert (ship_personality_target (SHIP_PERSONALITY_LOOTER, 20, 50, 70,
                                  80, 21, 90) == 90);
  assert (ship_personality_target (SHIP_PERSONALITY_LOOTER, 20, 50, 70,
                                  80, 21, 0) == 20);
  assert (ship_personality_target (SHIP_PERSONALITY_BLOCKADER, 20, 50, 70,
                                  80, 21, 90) == 50);
  assert (ship_personality_target (SHIP_PERSONALITY_BLOCKADER, 20, 0, 70,
                                  80, 21, 90) == 20);
  return 0;
}
