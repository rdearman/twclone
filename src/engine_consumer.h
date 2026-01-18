#pragma once
#include <stddef.h>
#include "db/db_api.h"

/* Configurable behaviour */
typedef struct
{
  int batch_size;		/* max events per tick (bounded work)        */
  int backlog_prio_threshold;	/* when lag >= this, run priority pass first */
  const char *priority_types_csv;	/* e.g. "s2s.broadcast.sweep,player.login"   */
  const char *consumer_key;	/* engine_offset.key e.g. "game_engine"      */
} eng_consumer_cfg_t;
/* Metrics returned per tick */
typedef struct
{
  long long last_event_id;	/* watermark *after* this tick               */
  long long lag;		/* max(events.id) - last_event_id            */
  int processed;		/* events applied in this tick               */
  int quarantined;		/* events sent to deadletter in this tick    */
} eng_consumer_metrics_t;
/* One tick: process up to cfg.batch_size events; update watermark durably. */
int engine_consume_tick (db_t * db,
			 const eng_consumer_cfg_t * cfg,
			 eng_consumer_metrics_t * out_metrics);
/* Register your event handlers here (returns 0 on success; non-0 to quarantine). */
int handle_event (const char *type, db_t * db,
		  db_res_t * ev_row /* bound row */ );
