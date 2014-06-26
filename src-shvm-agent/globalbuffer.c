#include "globalbuffer.h"

#include "shared/threadlocal.h"
#include "shared/buffpack.h"
#include "shared/messagetype.h"

#include "pbmanager.h"
#include "sender.h"

#include "../src-disl-agent/jvmtiutil.h"

static jrawMonitorID to_buff_lock;
static to_buff_struct to_buff_array[TO_BUFFER_COUNT];

static jvmtiEnv *jvmti_env;

void glbuffer_init(jvmtiEnv *env) {
  jvmti_env = env;
  jvmtiError error;

  error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "buffids", &to_buff_lock);
  check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");

  // initialize total ordering buff array
  for (int i = 0; i < TO_BUFFER_COUNT; ++i) {
    to_buff_array[i].pb = NULL;
  }
}

static void glbuffer_new(to_buff_struct *tobs, tldata * tld) {
  tobs->pb = pb_normal_get(tld->id);
  // set owner_id as t_buffid
  tobs->pb->owner_id = tld->to_buff_id;
  tobs->analysis_count = 0;
  tobs->analysis_count_pos = messager_analyze_header(tobs->pb->analysis_buff,
      tld->to_buff_id);
}

void glbuffer_commit() {
  tldata * tld = tld_get();

  enter_critical_section(jvmti_env, to_buff_lock);
  {
    // pointer to the total order buffer structure
    to_buff_struct * tobs = &(to_buff_array[tld->to_buff_id]);

    // allocate new buffer
    if (tobs->pb == NULL) {
      glbuffer_new(tobs, tld);
    }

    // fill total order buffers
    buffer_fill(tobs->pb->analysis_buff,
        // NOTE: normally access the buffer using methods
        tld->local_pb->analysis_buff->buff,
        tld->local_pb->analysis_buff->occupied);

    // empty local buffers (cheap)
    buffer_clean(tld->local_pb->analysis_buff);

    // add number of completed requests
    ++(tobs->analysis_count);

    // buffer has to be updated each time because jvm could end and buffer
    // has to be up-to date
    buff_put_int(tobs->pb->analysis_buff, tobs->analysis_count_pos,
        tobs->analysis_count);

    // send only when the method count is reached
    if (tobs->analysis_count >= ANALYSIS_COUNT) {
      // send buffers for object tagging
      sender_enqueue(tobs->pb);
      // invalidate buffer pointer
      tobs->pb = NULL;
    }
  }
  exit_critical_section(jvmti_env, to_buff_lock);
}

void glbuffer_sendall() {
  // send all total ordering buffers - with lock
  enter_critical_section(jvmti_env, to_buff_lock);
  {
    for (int i = 0; i < TO_BUFFER_COUNT; ++i) {
      // send all buffers for occupied ids
      if (to_buff_array[i].pb != NULL) {
        // send buffers for object tagging
        sender_enqueue(to_buff_array[i].pb);
        // invalidate buffer pointer
        to_buff_array[i].pb = NULL;
      }
    }
  }
  exit_critical_section(jvmti_env, to_buff_lock);
}
