#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include "common.h"


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}



/*
  init_sockaddr

  behavior: accepts a port, and a pointer to a sockaddr_in structure,
  returns a sockid corresponding to a bound socket that was made in the
  function.
*/
int
init_sockaddr (int port, struct sockaddr_in *sock)
{
  int sockid;

  sock->sin_family = AF_INET;
  sock->sin_port = htons (port);
  sock->sin_addr.s_addr = htonl (INADDR_ANY);

  if ((sockid = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("init_sockaddr: socket");
      exit (-1);
    }

  if (bind (sockid, (struct sockaddr *) sock, sizeof (*sock)) == -1)
    {
      perror ("init_sockaddr: bind");
      close (sockid);
      exit (-1);
    }

  if (listen (sockid, 7) == -1)
    {
      perror ("init_sockaddr: listen");
      close (sockid);
      exit (-1);
    }

  return sockid;
}

int
init_clientnetwork (char *hostname, int port)
{
  struct hostent *host;
  struct sockaddr_in serv_sockaddr;
  int sockid;

  if ((host = gethostbyname (hostname)) == (struct hostent *) NULL)
    {
      perror ("WRITER: gethostbyname");
      exit (-1);
    }

  //setting up the sockaddt pointing to the server
  serv_sockaddr.sin_family = AF_INET;
  memcpy (&serv_sockaddr.sin_addr, host->h_addr, host->h_length);
  serv_sockaddr.sin_port = htons (port);

  if ((sockid = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("socket: ");
      exit (-1);
    }

  if (connect
      (sockid, (struct sockaddr *) &serv_sockaddr,
       sizeof (serv_sockaddr)) == -1)
    {
      perror ("connect: ");
      exit (-1);
    }

  return sockid;

}

int
sendinfo (int sockid, char *buffer)
{
  if (send (sockid, buffer, strlen (buffer), 0) == -1)
    {
      perror ("sendto: ");
      close (sockid);
      return -1;
    }
  return 0;
}


int
recvinfo (int sockid, char *buffer)
{
  int len;
  char tempbuffer[BUFF_SIZE];

  if ((len = recv (sockid, tempbuffer, BUFF_SIZE, 0)) == -1)
    {
      perror ("recvfrom: ");
      close (sockid);
      return -1;
    }
  tempbuffer[len] = '\0';
  strncpy (buffer, tempbuffer, BUFF_SIZE);
  return 0;
}

int
acceptnewconnection (int sockid)
{
  int sockaid;
  socklen_t clnt_length;
  struct sockaddr_in *clnt_sockaddr = NULL;

  clnt_length = sizeof (*clnt_sockaddr);

  if ((sockaid = accept (sockid, (struct sockaddr *) clnt_sockaddr,
			 &clnt_length)) == -1)
    {
      perror ("accept: ");
      exit (-1);
    }

  return sockaid;
}

int
randomnum (int min, int max)
{
  return (min +
	  ((int)
	   ((double) rand () / ((double) RAND_MAX + 1) * (1 + max - min))));

}

// Helper function for random number generation (inclusive)
int
get_random_int (int min, int max)
{
  return min + rand () % (max - min + 1);
}



// Utility functions for random numbers and clamping
double
rand01 ()
{
  return (double) rand () / (double) RAND_MAX;
}

double
rand_range (double min_val, double max_val)
{
  return min_val + (rand01 () * (max_val - min_val));
}

double
clamp (double value, double min_val, double max_val)
{
  if (value < min_val)
    return min_val;
  if (value > max_val)
    return max_val;
  return value;
}

// Placeholder functions to satisfy the linker
void
doprocess ()
{
  // Placeholder for a function that handles processing.
}


#include <time.h>

void
now_iso8601 (char out[25])
{
  time_t t = time (NULL);
  struct tm tm;
  gmtime_r (&t, &tm);
  /* YYYY-MM-DDTHH:MM:SSZ -> 20 chars + NUL */
  strftime (out, 25, "%Y-%m-%dT%H:%M:%SZ", &tm);
}



void
strip_ansi (char *dst, const char *src, size_t cap)
{
  if (!dst || !src || cap == 0)
    return;
  size_t w = 0;
  for (size_t r = 0; src[r] != '\0' && w + 1 < cap;)
    {
      unsigned char c = (unsigned char) src[r];
      if (c == 0x1B)
	{			/* ESC */
	  /* Skip CSI: ESC '[' ... letter */
	  r++;
	  if (src[r] == '[')
	    {
	      r++;
	      while (src[r] && !(src[r] >= '@' && src[r] <= '~'))
		r++;		/* params */
	      if (src[r])
		r++;		/* consume final byte */
	      continue;
	    }
	  /* Skip OSC: ESC ']' ... BEL or ST (ESC '\') */
	  if (src[r] == ']')
	    {
	      r++;
	      while (src[r] && src[r] != 0x07)
		{
		  if (src[r] == 0x1B && src[r + 1] == '\\')
		    {
		      r += 2;
		      break;
		    }
		  r++;
		}
	      if (src[r] == 0x07)
		r++;		/* BEL */
	      continue;
	    }
	  /* Fallback: drop single ESC */
	  continue;
	}
      dst[w++] = src[r++];
    }
  dst[w] = '\0';
}

const char *
get_tow_reason_string (int reason_code)
{
  switch (reason_code)
    {
    case ERR_UNKNOWN:
      return "Unknown error.";
    case ERR_NOT_IMPLEMENTED:
      return "Feature not implemented.";
    case ERR_SERVICE_UNAVAILABLE:
      return "Service temporarily unavailable.";
    case ERR_MAINTENANCE_MODE:
      return "System in maintenance mode.";
    case ERR_TIMEOUT:
      return "Request timed out.";
    case ERR_DUPLICATE_REQUEST:
      return "Duplicate request, already processed.";
    case ERR_SERIALIZATION:
      return "Serialization error.";
    case ERR_VERSION_NOT_SUPPORTED:
      return "Protocol version not supported.";
    case ERR_MESSAGE_TOO_LARGE:
      return "Message too large.";
    case ERR_RATE_LIMITED:
      return "Rate limit exceeded.";
    case ERR_INSUFFICIENT_TURNS:
      return "Insufficient turns to perform action.";
    case ERR_PERMISSION_DENIED:
      return "Permission denied.";
    case ERR_NOT_FOUND:
      return "Resource not found.";
    case ERR_DB:
      return "Database error.";
    case ERR_OOM:
      return "Out of memory.";
    case ERR_DB_QUERY_FAILED:
      return "Database query failed.";
    case ERROR_INTERNAL:
      return "Internal server error.";
    case ERR_MEMORY:
      return "Memory allocation error.";

    case ERR_NOT_AUTHENTICATED:
      return "Authentication required.";
    case ERR_INVALID_TOKEN:
      return "Invalid authentication token.";
    case ERR_TOKEN_EXPIRED:
      return "Authentication token expired.";
    case ERR_SESSION_REVOKED:
      return "Session revoked.";
    case ERR_USER_NOT_FOUND:
      return "User not found.";
    case ERR_NAME_TAKEN:
      return "Username already taken.";
    case ERR_WEAK_PASSWORD:
      return "Password is too weak.";
    case ERR_MFA_REQUIRED:
      return "Multi-factor authentication required.";
    case ERR_MFA_INVALID:
      return "Invalid MFA code.";
    case ERR_PLAYER_BANNED:
      return "Player is banned.";
    case ERR_ALIGNMENT_RESTRICTED:
      return "Action restricted by alignment.";
    case ERR_REF_BIG_SLEEP:
      return "Player is in 'Big Sleep' and cannot log in.";
    case ERR_INVALID_CREDENTIAL:
      return "Invalid credentials.";
    case ERR_REGISTRATION_DISABLED:
      return "New player registration is disabled.";
    case ERR_IS_NPC:
      return "NPC accounts cannot log in.";
    case ERR_NOT_IN_CORP:
      return "Not a member of a corporation.";

    case ERR_INVALID_SCHEMA:
      return "Invalid request schema.";
    case ERR_MISSING_FIELD:
      return "Missing required field.";
    case ERR_INVALID_ARG:
      return "Invalid argument provided.";
    case ERR_OUT_OF_RANGE:
      return "Value out of allowed range.";
    case ERR_LIMIT_EXCEEDED:
      return "Limit exceeded.";
    case ERR_TOO_MANY_BULK_ITEMS:
      return "Too many items in bulk request.";
    case ERR_CURSOR_INVALID:
      return "Invalid cursor for pagination.";
    case ERR_BAD_REQUEST:
      return "Bad request.";
    case ERR_REF_NO_TURNS:
      return "No turns remaining to perform action.";
    case ERR_CONFIRMATION_REQUIRED:
      return "Confirmation required for this action.";
    case REF_INSUFFICIENT_CAPACITY:
      return "Insufficient capacity.";

    case REF_NOT_IN_SECTOR:
      return "Player is not in the specified sector.";
    case ERR_SECTOR_NOT_FOUND:
      return "Sector not found.";
    case REF_NO_WARP_LINK:
      return "No warp link to target sector.";
    case REF_TURN_COST_EXCEEDS:
      return "Turn cost exceeds available turns.";
    case REF_AUTOPILOT_RUNNING:
      return "Autopilot is already running.";
    case ERR_AUTOPILOT_PATH_INVALID:
      return "Autopilot path is invalid.";
    case REF_SAFE_ZONE_ONLY:
      return "Action only allowed in safe zones.";
    case REF_BLOCKED_BY_MINES:
      return "Movement blocked by mines.";
    case REF_TRANSWARP_UNAVAILABLE:
      return "Transwarp drive unavailable or damaged.";
    case ERR_BAD_STATE:
      return "Bad state encountered.";
    case TERRITORY_UNSAFE:
      return "Territory is unsafe for this action.";
    case ERR_FOREIGN_LIMPETS_PRESENT:
      return "Foreign limpets present, cannot perform action.";

    case ERR_GENESIS_DISABLED:
      return "Genesis feature is currently disabled.";
    case ERR_GENESIS_MSL_PROHIBITED:
      return "Planet creation prohibited in MSL sector.";
    case ERR_GENESIS_SECTOR_FULL:
      return "Sector has reached maximum planets.";
    case ERR_NO_GENESIS_TORPEDO:
      return "Insufficient Genesis torpedoes on your ship.";
    case ERR_INVALID_PLANET_NAME_LENGTH:
      return "Planet name too long or invalid.";
    case ERR_INVALID_OWNER_TYPE:
      return "Invalid owner entity type. Must be 'player' or 'corporation'.";
    case ERR_NO_CORPORATION:
      return "Player is not in a corporation to create a corporate planet.";
    case ERR_UNIVERSE_FULL:
      return "The universe has reached its maximum planet capacity.";

    case ERR_PLANET_NOT_FOUND:
      return "Planet not found.";
    case REF_NOT_PLANET_OWNER:
      return "Not the owner of this planet.";
    case REF_LANDING_REFUSED:
      return "Landing refused.";
    case ERR_CITADEL_REQUIRED:
      return "Citadel required.";
    case ERR_CITADEL_MAX_LEVEL:
      return "Citadel at maximum level.";
    case REF_INSUFFICIENT_RES:
      return "Insufficient resources.";
    case REF_TRANSFER_NOT_PERMITTED:
      return "Transfer not permitted.";
    case REF_GENESIS_DISABLED:
      return "Genesis is disabled for this planet.";

    case ERR_PORT_NOT_FOUND:
      return "Port not found.";
    case REF_PORT_OUT_OF_STOCK:
      return "Port out of stock.";
    case REF_PRICE_SLIPPAGE:
      return "Price slippage occurred.";
    case REF_DOCKING_REFUSED:
      return "Docking refused.";
    case ERR_LICENSE_REQUIRED:
      return "License required.";
    case REF_PORT_BLACKLISTED:
      return "Port is blacklisted.";
    case REF_PORT_CLOSED:
      return "Port is closed.";

    case ERR_COMMODITY_UNKNOWN:
      return "Unknown commodity.";
    case REF_NOT_ENOUGH_HOLDS:
      return "Not enough holds available.";
    case ERR_INSUFFICIENT_FUNDS:
      return "Insufficient funds.";
    case ERR_OFFER_NOT_FOUND:
      return "Offer not found.";
    case REF_OFFER_EXPIRED:
      return "Offer expired.";
    case REF_OFFER_NOT_YOURS:
      return "Offer is not yours.";
    case REF_TRADE_WINDOW_CLOSED:
      return "Trade window closed.";
    case ERR_COMMODITY_NOT_SOLD:
      return "Commodity not sold here.";
    case ERR_PRICE_INVALID:
      return "Invalid price.";

    case ERR_SHIP_NOT_FOUND:
      return "Ship not found.";
    case ERR_TARGET_INVALID:
      return "Invalid target.";
    case REF_FRIENDLY_FIRE_BLOCKED:
      return "Friendly fire blocked.";
    case REF_COMBAT_DISALLOWED:
      return "Combat disallowed.";
    case REF_AMMO_DEPLETED:
      return "Ammo depleted.";
    case REF_HULL_CRITICAL:
      return "Hull critical.";
    case REF_MINE_LIMIT_EXCEEDED:
      return "Mine limit exceeded.";
    case REF_DESTROYED_TERMINAL:
      return "Ship destroyed.";
    case ERR_HARDWARE_NOT_AVAILABLE:
      return "Hardware item is not available at this location.";
    case ERR_HARDWARE_INVALID_ITEM:
      return "Invalid hardware item code.";
    case ERR_HARDWARE_INSUFFICIENT_FUNDS:
      return "Insufficient credits on ship for purchase.";
    case ERR_HARDWARE_CAPACITY_EXCEEDED:
      return "Purchase would exceed ship's capacity for this item.";
    case ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP:
      return "Your ship type does not support this hardware item.";
    case ERR_HARDWARE_QUANTITY_INVALID:
      return "Invalid purchase quantity. Must be greater than zero.";

    case ERR_RECIPIENT_NOT_FOUND:
      return "Recipient not found.";
    case REF_MUTED_OR_BLOCKED:
      return "Player muted or blocked.";
    case REF_BROADCAST_FORBIDDEN:
      return "Broadcast forbidden.";
    case REF_INBOX_FULL:
      return "Inbox full.";
    case ERR_MESSAGE_TOO_LONG:
      return "Message too long.";

    case ERR_REPLICATION_LAG:
      return "Replication lag detected.";
    case ERR_S2S_CONFLICT:
      return "Server-to-server conflict.";
    case REF_ADMIN_ONLY:
      return "Admin access required.";
    case ERR_SHARD_UNAVAILABLE:
      return "Shard unavailable.";
    case ERR_CAPABILITY_DISABLED:
      return "Capability disabled.";
    case ERR_LIMPETS_DISABLED:
      return "Limpets disabled.";
    case ERR_LIMPET_SWEEP_DISABLED:
      return "Limpet sweep disabled.";
    case ERR_LIMPET_ATTACK_DISABLED:
      return "Limpet attack disabled.";

    case SECTOR_ERR:
      return "Sector error.";
    case ERR_SECTOR_OVERCROWDED:
      return "Sector is overcrowded.";
    case ERR_FORBIDDEN_IN_SECTOR:
      return "Forbidden action in this sector.";

    case REASON_EVIL_ALIGN:
      return "Evil alignment.";
    case REASON_EXCESS_FIGHTERS:
      return "Excess fighters.";
    case REASON_HIGH_EXP:
      return "High experience.";
    case REASON_NO_OWNER:
      return "No owner.";
    case REASON_OVERCROWDING:
      return "Overcrowding.";

    default:
      return "An unknown error occurred.";
    }
}


// Implementation of json_get_int_flexible
bool
json_get_int_flexible (json_t *data_obj, const char *key, int *out_val)
{
  if (!json_is_object (data_obj))
    {
      return false;
    }
  json_t *val = json_object_get (data_obj, key);
  if (json_is_integer (val))
    {
      *out_val = (int) json_integer_value (val);
      return true;
    }
  if (json_is_string (val))
    {
      const char *s = json_string_value (val);
      if (!s || !*s)
	return false;
      char *endptr;
      long lval = strtol (s, &endptr, 10);
      if (*endptr == '\0' && endptr != s)
	{			// Check that conversion actually happened
	  *out_val = (int) lval;
	  return true;
	}
    }
  return false;
}

// Implementation of json_get_int64_flexible
bool
json_get_int64_flexible (json_t *json, const char *key, long long *out)
{
  if (!json || !key || !out)
    return false;

  json_t *value = json_object_get (json, key);
  if (!value)
    return false;

  if (json_is_integer (value))
    {
      *out = json_integer_value (value);
      return true;
    }
  else if (json_is_string (value))
    {
      // Attempt to parse string as long long
      long long ll_val;
      if (sscanf (json_string_value (value), "%lld", &ll_val) == 1)
	{
	  *out = ll_val;
	  return true;
	}
    }
  return false;
}

// Implementation of json_get_string_or_null
const char *
json_get_string_or_null (json_t *data_obj, const char *key)
{
  if (!json_is_object (data_obj))
    {
      return NULL;
    }
  json_t *val = json_object_get (data_obj, key);
  if (json_is_string (val))
    {
      return json_string_value (val);
    }
  return NULL;
}
