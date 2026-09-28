#ifndef SHIP_PERSONALITY_H
#define SHIP_PERSONALITY_H

#include <strings.h>

typedef enum
{
  SHIP_PERSONALITY_BALANCED = 0,
  SHIP_PERSONALITY_OFFENSIVE,
  SHIP_PERSONALITY_DEFENSIVE,
  SHIP_PERSONALITY_LOOTER,
  SHIP_PERSONALITY_BLOCKADER
} ship_personality_t;

static inline ship_personality_t
ship_personality_parse (const char *value)
{
  if (!value)
    return SHIP_PERSONALITY_BALANCED;
  if (strcasecmp (value, "offensive") == 0)
    return SHIP_PERSONALITY_OFFENSIVE;
  if (strcasecmp (value, "defensive") == 0)
    return SHIP_PERSONALITY_DEFENSIVE;
  if (strcasecmp (value, "looter") == 0)
    return SHIP_PERSONALITY_LOOTER;
  if (strcasecmp (value, "blockader") == 0)
    return SHIP_PERSONALITY_BLOCKADER;
  return SHIP_PERSONALITY_BALANCED;
}

static inline ship_personality_t
ship_personality_resolve (const char *ship_override,
                          const char *corporation_default,
                          const char *shiptype_default)
{
  if (ship_override && *ship_override)
    return ship_personality_parse (ship_override);
  if (corporation_default && *corporation_default)
    return ship_personality_parse (corporation_default);
  if (shiptype_default && *shiptype_default)
    return ship_personality_parse (shiptype_default);
  return SHIP_PERSONALITY_BALANCED;
}

static inline const char *
ship_personality_name (ship_personality_t personality)
{
  switch (personality)
    {
    case SHIP_PERSONALITY_OFFENSIVE: return "offensive";
    case SHIP_PERSONALITY_DEFENSIVE: return "defensive";
    case SHIP_PERSONALITY_LOOTER: return "looter";
    case SHIP_PERSONALITY_BLOCKADER: return "blockader";
    case SHIP_PERSONALITY_BALANCED:
    default: return "balanced";
    }
}

static inline int
ship_personality_target (ship_personality_t personality, int current_sector,
                         int home_sector, int balanced_target,
                         int random_sector, int adjacent_sector,
                         int port_sector)
{
  switch (personality)
    {
    case SHIP_PERSONALITY_OFFENSIVE:
      return random_sector > 10 ? random_sector : current_sector;
    case SHIP_PERSONALITY_DEFENSIVE:
      return adjacent_sector > 0 ? adjacent_sector : current_sector;
    case SHIP_PERSONALITY_LOOTER:
      return port_sector > 0 ? port_sector : current_sector;
    case SHIP_PERSONALITY_BLOCKADER:
      return home_sector > 0 ? home_sector : current_sector;
    case SHIP_PERSONALITY_BALANCED:
    default:
      return balanced_target > 0 ? balanced_target : current_sector;
    }
}

#endif
