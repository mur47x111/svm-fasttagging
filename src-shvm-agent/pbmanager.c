#include "pbmanager.h"

#include "shared/blockingqueue.h"
#include "shared/threadlocal.h"

#include "../src-disl-agent/jvmtiutil.h"

// queue with empty buffers
static blocking_queue empty_q;

// queue with empty utility buffers
static blocking_queue utility_q;

// list of all allocated bq buffers
process_buffs pb_list[BQ_BUFFERS + BQ_UTILITY];

void pb_init() {
  bq_create(&utility_q, BQ_UTILITY, sizeof(process_buffs *));
  bq_create(&empty_q, BQ_BUFFERS, sizeof(process_buffs *));

  for (int i = 0; i < BQ_BUFFERS + BQ_UTILITY; i++) {
    process_buffs * pb = &(pb_list[i]);

    // allocate process_buffs
    pb->analysis_buff = malloc(sizeof(buffer));
    buffer_alloc(pb->analysis_buff);
    pb->command_buff = malloc(sizeof(buffer));
    buffer_alloc(pb->command_buff);

    if (i < BQ_BUFFERS) {
      // add buffer to the empty queue
      pb_normal_release(pb);
    } else {
      // add buffer to the utility queue
      pb_utility_release(pb);
    }
  }
}

void pb_free() {
  // NOTE: Buffers hold by other threads can be in inconsistent state.
  // We cannot simply send them, so we at least inform the user.

  // inform about all non-send buffers
  // all buffers should be send except some daemon thread buffers
  //  - also some class loading + thread tagging buffers can be there (with 0)
  // Report: .

  int relevant_count = 0;
  int support_count = 0;
  int marked_thread_count = 0;
  int non_marked_thread_count = 0;

  for (int i = 0; i < BQ_BUFFERS; ++i) {
    // buffer held by thread that performed (is still doing) analysis
    //  - probably analysis data
    if (pb_list[i].owner_id >= STARTING_THREAD_ID) {
      relevant_count += buffer_filled(pb_list[i].analysis_buff);
      support_count += buffer_filled(pb_list[i].command_buff);
      ++marked_thread_count;
#ifdef DEBUG
      printf("Lost buffer for id %ld\n", pb_list[i].owner_id);
#endif
    }

    // buffer held by thread that did NOT perform analysis
    //  - support data
    if (pb_list[i].owner_id == INVALID_THREAD_ID) {
      support_count += buffer_filled(pb_list[i].analysis_buff)
          + buffer_filled(pb_list[i].command_buff);
      ++non_marked_thread_count;
    }

    check_error(pb_list[i].owner_id == PB_OBJTAG,
        "Unprocessed buffers left in object tagging queue");

    check_error(pb_list[i].owner_id == PB_SEND,
        "Unprocessed buffers left in sending queue");
  }

#ifdef DEBUG
  if(relevant_count > 0 || support_count > 0) {
    fprintf(stderr, "%s%s%d%s%d%s%s%d%s%d%s",
        "Warning: ",
        "Due to non-terminated (daemon) threads, ",
        relevant_count,
        " bytes of relevant data and ",
        support_count,
        " bytes of support data were lost ",
        "(thread count - analysis: ",
        marked_thread_count,
        ", helper: ",
        non_marked_thread_count,
        ").\n");
  }
#endif
}

process_buffs * pb_get(jlong thread_id) {
  for (int i = 0; i < BQ_BUFFERS; ++i) {
    // if buffer is owned by tagged thread, send it
    if (pb_list[i].owner_id == thread_id) {
      return &(pb_list[i]);
    }
  }

  return NULL;
}

process_buffs * pb_normal_get(jlong thread_id) {
  // retrieves pointer to buffer
  process_buffs * buffs;
  bq_pop(&empty_q, &buffs);
  buffs->owner_id = thread_id;
  return buffs;
}

// normally only sending thread should access this function
void pb_normal_release(process_buffs * buffs) {
  // empty buff
  buffer_clean(buffs->analysis_buff);
  buffer_clean(buffs->command_buff);

  // stores pointer to buffer
  buffs->owner_id = PB_FREE;
  bq_push(&empty_q, &buffs);
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
