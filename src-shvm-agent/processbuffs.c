#include "processbuffs.h"
#include "blockingqueue.h"
#include "netref.h"

#include "../src-disl-agent/jvmtiutil.h"

// queue with empty buffers
static blocking_queue empty_q;

// queue with empty utility buffers
static blocking_queue utility_q;

void pb_init() {
  bq_create(&utility_q, BQ_UTILITY, sizeof(process_buffs *));
  bq_create(&empty_q, BQ_BUFFERS, sizeof(process_buffs *));
}

process_buffs * pb_get(jlong thread_id) {
  // retrieves pointer to buffer
  process_buffs * buffs;
  bq_pop(&empty_q, &buffs);
  buffs->owner_id = thread_id;
  return buffs;
}

process_buffs * pb_utility_get() {
  // retrieves pointer to buffer
  process_buffs * buffs;
  bq_pop(&utility_q, &buffs);

  // no owner setting - it is already PB_UTILITY
  return buffs;
}

// normally only sending thread should access this function
void pb_utility_release(process_buffs * buffs) {
  // empty buff
  buffer_clean(buffs->analysis_buff);
  buffer_clean(buffs->command_buff);

  // stores pointer to buffer
  buffs->owner_id = PB_UTILITY;
  bq_push(&utility_q, &buffs);
}

// normally only sending thread should access this function
void pb_release(process_buffs * buffs) {
  // empty buff
  buffer_clean(buffs->analysis_buff);
  buffer_clean(buffs->command_buff);

  // stores pointer to buffer
  buffs->owner_id = PB_FREE;
  bq_push(&empty_q, &buffs);
}
