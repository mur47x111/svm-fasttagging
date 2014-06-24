#include "freehandler.h"

#include "shared/buffer.h"
#include "shared/buffpack.h"
#include "shared/messagetype.h"

#include "pbmanager.h"
#include "sender.h"

#include "../src-disl-agent/jvmtiutil.h"

static jvmtiEnv * jvmti_env;
static jrawMonitorID obj_free_lock;

#define MAX_OBJ_FREE_EVENTS 4096

static process_buffs * obj_free_buff = NULL;
static jint obj_free_event_count = 0;
static size_t obj_free_event_count_pos = 0;

void fh_init(jvmtiEnv *env) {
  jvmti_env = env;

  jvmtiError error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "obj free",
      &obj_free_lock);
  check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");
}

void fh_object_free(jlong tag) {
  enter_critical_section(jvmti_env, obj_free_lock);
  {
    // allocate new obj free buffer
    if (obj_free_buff == NULL) {
      // obtain buffer
      obj_free_buff = pb_utility_get();
      // reset number of events in the buffer
      obj_free_event_count = 0;
      // get pointer to the location where count of requests will stored
      obj_free_event_count_pos = messager_objfree_header(
          obj_free_buff->analysis_buff);
    }

    // obtain message buffer
    buffer * buff = obj_free_buff->analysis_buff;
    messager_objfree_item(buff, tag);

    // update the number of free events
    ++obj_free_event_count;
    buff_put_int(buff, obj_free_event_count_pos, obj_free_event_count);

    if (obj_free_event_count >= MAX_OBJ_FREE_EVENTS) {
      // NOTE: We can queue buffer to the sending queue. This is because
      // object tagging thread is first sending the objects and then
      // deallocating the global references. We cannot have here objects
      // that weren't send already

      // NOTE2: It is mandatory to submit to the sending queue directly
      // because gc (that is generating these events) will block the
      // tagging thread. And with not working tagging thread, we can
      // run out of buffers.
      sender_enqueue(obj_free_buff);

      // cleanup
      obj_free_buff = NULL;
      obj_free_event_count = 0;
      obj_free_event_count_pos = 0;
    }

  }
  exit_critical_section(jvmti_env, obj_free_lock);
}

void fh_send_buffer() {
  // send object free buffer - with lock
  enter_critical_section(jvmti_env, obj_free_lock);
  {
    if (obj_free_buff != NULL) {
      sender_enqueue(obj_free_buff);
      obj_free_buff = NULL;
    }
  }
  exit_critical_section(jvmti_env, obj_free_lock);
}

