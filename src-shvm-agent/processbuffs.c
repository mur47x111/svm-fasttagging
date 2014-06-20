#include "processbuffs.h"


process_buffs * buffs_get(jlong thread_id) {
  // retrieves pointer to buffer
  process_buffs * buffs;
  bq_pop(&empty_q, &buffs);
  buffs->owner_id = thread_id;
  return buffs;
}

process_buffs * buffs_utility_get() {
  // retrieves pointer to buffer
  process_buffs * buffs;
  bq_pop(&utility_q, &buffs);

  // no owner setting - it is already PB_UTILITY
  return buffs;
}

process_buffs * buffs_utility_send(process_buffs * buffs) {
  // no owner setting - it is already PB_UTILITY
  bq_push(&send_q, &buffs);
  return buffs;
}

// normally only sending thread should access this function
void _buffs_utility_release(process_buffs * buffs) {
  // empty buff
  buffer_clean(buffs->analysis_buff);
  buffer_clean(buffs->command_buff);

  // stores pointer to buffer
  buffs->owner_id = PB_UTILITY;
  bq_push(&utility_q, &buffs);
}

// normally only sending thread should access this function
void _buffs_release(process_buffs * buffs) {
  // empty buff
  buffer_clean(buffs->analysis_buff);
  buffer_clean(buffs->command_buff);

  // stores pointer to buffer
  buffs->owner_id = PB_FREE;
  bq_push(&empty_q, &buffs);
}

void buffs_objtag(process_buffs * buffs) {
  buffs->owner_id = PB_OBJTAG;
  bq_push(&objtag_q, &buffs);
}

// only objtag thread should access this function
process_buffs * _buffs_objtag_get() {
  process_buffs * buffs;
  bq_pop(&objtag_q, &buffs);
  return buffs;
}

void _buffs_send(process_buffs * buffs) {
  buffs->owner_id = PB_SEND;
  bq_push(&send_q, &buffs);
}

// only sending thread should access this function
process_buffs * _buffs_send_get() {
  process_buffs * buffs;
  bq_pop(&send_q, &buffs);
  return buffs;
}

