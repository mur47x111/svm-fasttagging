#include "tlocalbuffer.h"

#include "shared/threadlocal.h"
#include "shared/messagetype.h"
#include "shared/buffpack.h"

#include "pbmanager.h"
#include "tagger.h"
#include "globalbuffer.h"

#include "../src-disl-agent/jvmtiutil.h"

// initial ids are reserved for total ordering buffers
static volatile jlong avail_thread_id = STARTING_THREAD_ID;

static inline jlong next_thread_id() {
  return __sync_fetch_and_add(&avail_thread_id, 1);
}

void tl_insert_analysis_item(jshort analysis_method_id) {
  tldata * tld = tld_get();

  if (tld->analysis_buff == NULL) {

    // mark thread
    if (tld->id == INVALID_THREAD_ID) {
      tld->id = next_thread_id();
    }

    // get buffers
    tld->pb = pb_normal_get(tld->id);
    tld->analysis_buff = tld->pb->analysis_buff;
    tld->command_buff = tld->pb->command_buff;

    // determines, how many analysis requests are sent in one message
    tld->analysis_count = 0;

    // create analysis message
    tld->analysis_count_pos = messager_analyze_header(tld->analysis_buff,
        tld->id);
  }

  // create request header, keep track of the position
  // of the length of marshalled arguments
  tld->args_length_pos = messager_analyze_item(tld->analysis_buff,
      analysis_method_id);
}

void tl_insert_analysis_item_ordering(jshort analysis_method_id,
    jbyte ordering_id) {
  check_error(ordering_id < 0, "Buffer id has negative value");
  tldata * tld = tld_get();

  // flush normal buffers before each global buffering
  if (tld->analysis_buff != NULL) {
    // invalidate buffer pointers
    tld->analysis_buff = NULL;
    tld->command_buff = NULL;

    // send buffers for object tagging
    tagger_enqueue(tld->pb);
    // invalidate buffer pointer
    tld->pb = NULL;
  }

  // allocate special local buffer for this buffering
  if (tld->local_pb == NULL) {
    // mark thread
    if (tld->id == INVALID_THREAD_ID) {
      tld->id = next_thread_id();
    }

    // get buffers
    tld->local_pb = pb_normal_get(tld->id);
  }

  // set local buffers for this buffering
  tld->analysis_buff = tld->local_pb->analysis_buff;
  tld->command_buff = tld->local_pb->command_buff;

  tld->to_buff_id = ordering_id;

  // create request header, keep track of the position
  // of the length of marshalled arguments
  tld->args_length_pos = messager_analyze_item(tld->analysis_buff,
      analysis_method_id);
}

void tl_analysis_end() {
  tldata * tld = tld_get();

  // update the length of the marshalled arguments
  jshort args_length = buffer_filled(tld->analysis_buff) - tld->args_length_pos
      - sizeof(jshort);
  buff_put_short(tld->analysis_buff, tld->args_length_pos, args_length);

  // this method is also called for end of analysis for totally ordered API
  if (tld->to_buff_id != INVALID_BUFF_ID) {
    // TODO lock for each buffer id
    // sending of half-full buffer is done in shutdown hook and obj free hook
    // write analysis to total order buffer - with lock
    glbuffer_commit();

    // reset analysis and command buffers for normal buffering
    // set to NULL, because we've send the buffers at the beginning of
    // global buffer buffering
    tld->analysis_buff = NULL;
    tld->command_buff = NULL;

    // invalidate buffer id
    tld->to_buff_id = INVALID_BUFF_ID;
  } else {
    // sending of half-full buffer is done in thread end hook
    // increment the number of completed requests
    tld->analysis_count++;

    // buffer has to be updated each time - the thread can end any time
    buff_put_int(tld->analysis_buff, tld->analysis_count_pos,
        tld->analysis_count);

    // send only after the proper count is reached
    if (tld->analysis_count >= ANALYSIS_COUNT) {
      // invalidate buffer pointers
      tld->analysis_buff = NULL;
      tld->command_buff = NULL;

      // send buffers for object tagging
      tagger_enqueue(tld->pb);

      // invalidate buffer pointer
      tld->pb = NULL;
    }
  }
}

void tl_send_buffer() {
  tldata * tld = tld_get();

  // thread is marked -> worked with buffers
  jlong thread_id = tld->id;
  if (thread_id != INVALID_THREAD_ID) {
    process_buffs *pb = pb_get(thread_id);

    if (pb != NULL) {
      tagger_enqueue(pb);
    }
  }

  tld->analysis_buff = NULL;
  tld->command_buff = NULL;
  tld->pb = NULL;
}

void tl_thread_end() {
  // It should be safe to use thread locals according to jvmti documentation:
  // Thread end events are generated by a terminating thread after its initial
  // method has finished execution.
  jlong thread_id = tld_get()->id;

  if (thread_id == INVALID_THREAD_ID) {
    return;
  }

  // send all pending buffers associated with this thread
  tl_send_buffer();

  // send thread end message
  process_buffs * buffs = pb_normal_get(thread_id);
  messager_threadend_header(buffs->analysis_buff, thread_id);

  // send to object tagging queue - this thread could have something still
  // in the queue so we ensure proper ordering
  tagger_enqueue(buffs);
}