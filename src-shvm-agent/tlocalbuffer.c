#include "tlocalbuffer.h"

#include "shared/threadlocal.h"
#include "shared/messagetype.h"
#include "shared/buffpack.h"

#include "objecttag.h"
#include "pbmanager.h"
#include "globalbuffer.h"
#include "sender.h"

#include "../src-disl-agent/jvmtiutil.h"

static jvmtiEnv * jvmti_env;

static volatile jclass THREAD_CLASS = NULL;
static volatile jclass STRING_CLASS = NULL;

#ifdef DEBUGMETRICS

// first available object id
static volatile unsigned long jni_start_counter = 0;
// first available class id
static volatile unsigned long jni_all_counter = 0;

void tl_increase_start_counter() {
  __sync_fetch_and_add(&jni_start_counter, 1);
}

void tl_increase_all_counter() {
  __sync_fetch_and_add(&jni_all_counter, 1);
}

#endif

// ******************* Object tagging thread *******************

// TODO add cache - ??

static void pack_string_data(JNIEnv * jni_env, jstring to_send,
    jlong str_tag) {

  // get string length
  jsize str_len = (*jni_env)->GetStringUTFLength(jni_env, to_send);

  // get string data as utf-8
  const char * str = (*jni_env)->GetStringUTFChars(jni_env, to_send, NULL);
  check_error(str == NULL, "Cannot get string from java");

  // check if the size is sendable
  int size_fits = str_len < UINT16_MAX;
  check_error(!size_fits, "Java string is too big for sending");

  // add message to the buffer
  sender_stringinfo(str_tag, str, str_len);

  // release string
  (*jni_env)->ReleaseStringUTFChars(jni_env, to_send, str);
}

static void pack_thread_data(JNIEnv * jni_env, jstring to_send,
    jlong thr_tag) {

  jvmtiThreadInfo info;
  jvmtiError error = (*jvmti_env)->GetThreadInfo(jvmti_env, to_send, &info);
  check_error(error != JVMTI_ERROR_NONE, "Cannot get tread info");

  // pack thread info message
  sender_threadinfo(thr_tag, info.name, strlen(info.name), info.is_daemon);
}

static void pack_aditional_data(JNIEnv * jni_env, jlong * net_ref,
    jobject to_send) {
  // NOTE: we don't use lock for updating send status, so it is possible
  // that multiple threads will send it, but this will hurt only performance

  // NOTE: Tests for class types could be done by buffering threads.
  //       It depends, where we want to have the load.
  // String - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, STRING_CLASS)) {
    pack_string_data(jni_env, to_send, *net_ref);
    ot_set_spec(to_send, *net_ref);
  }

  // Thread - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, THREAD_CLASS)) {
    pack_thread_data(jni_env, to_send, *net_ref);
    ot_set_spec(to_send, *net_ref);
  }
}

void tl_pack_object(JNIEnv * jni_env, buffer * buff, jobject to_send,
    bool sent_data) {
  // pack null net reference
  jlong tag = ot_get_tag(jni_env, to_send);
  pack_long(buff, tag);

  if (sent_data && !ot_is_spec_set(tag)) {
    pack_aditional_data(jni_env, &tag, to_send);
  }
}

// ******************* analysis helper methods *******************

// first available id for new messages
static volatile jshort avail_analysis_id = 1;

static inline jlong next_analysis_id() {
  return __sync_fetch_and_add(&avail_analysis_id, 1);
}

jshort tl_register_method(JNIEnv * jni_env, jstring analysis_method_desc,
    jlong thread_id) {
  // *** send register analysis method message ***

  // request unique id
  jshort new_analysis_id = next_analysis_id();

  // get string length
  jsize str_len = (*jni_env)->GetStringUTFLength(jni_env, analysis_method_desc);

  // get string data as utf-8
  const char * str = (*jni_env)->GetStringUTFChars(jni_env,
      analysis_method_desc, NULL);
  check_error(str == NULL, "Cannot get string from java");

  // check if the size is sendable
  int size_fits = str_len < UINT16_MAX;
  check_error(!size_fits, "Java string is too big for sending");

  // obtain buffer
  process_buffs * buffs = pb_utility_get();
  messager_reganalysis_header(buffs->analysis_buff, new_analysis_id, str,
      str_len);
  sender_enqueue(buffs);

  // release string
  (*jni_env)->ReleaseStringUTFChars(jni_env, analysis_method_desc, str);
  return new_analysis_id;
}

// initial ids are reserved for total ordering buffers
static volatile jlong avail_thread_id = STARTING_THREAD_ID;

static inline jlong next_thread_id() {
  return __sync_fetch_and_add(&avail_thread_id, 1);
}

void tl_insert_analysis_item(jshort analysis_method_id) {
#ifdef DEBUGMETRICS
  tl_increase_start_counter();
#endif

  tldata * tld = tld_get();

  if (tld->analysis_buff == NULL) {

    // mark thread
    if (tld->id == INVALID_THREAD_ID) {
      tld->id = next_thread_id();
    }

    // get buffers
    tld->pb = pb_normal_get(tld->id);
    tld->analysis_buff = tld->pb->analysis_buff;

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
#ifdef DEBUGMETRICS
  tl_increase_start_counter();
#endif

  check_error(ordering_id < 0, "Buffer id has negative value");
  tldata * tld = tld_get();

  // flush normal buffers before each global buffering
  if (tld->analysis_buff != NULL) {
    // invalidate buffer pointers
    tld->analysis_buff = NULL;
    tld->command_buff = NULL;

    // send buffers for object tagging
    sender_enqueue(tld->pb);
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
      sender_enqueue(tld->pb);

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
      sender_enqueue(pb);
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
  sender_enqueue(buffs);
}

void tl_init(JNIEnv * jni_env, jvmtiEnv * jvmti) {
  jvmti_env = jvmti;

  if (STRING_CLASS == NULL) {
    STRING_CLASS = (*jni_env)->NewGlobalRef(jni_env,
        (*jni_env)->FindClass(jni_env, "java/lang/String"));
    check_error(STRING_CLASS == NULL, "String class not found");
  }

  if (THREAD_CLASS == NULL) {
    THREAD_CLASS = (*jni_env)->NewGlobalRef(jni_env,
        (*jni_env)->FindClass(jni_env, "java/lang/Thread"));
    check_error(THREAD_CLASS == NULL, "Thread class not found");
  }
}

void tl_print_counters() {
#ifdef DEBUGMETRICS
  printf("# of analysisStart: %ld\n", jni_start_counter);
  printf("# of JNI invocation: %ld\n", jni_all_counter);
#endif
}
