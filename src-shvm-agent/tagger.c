#include <stdlib.h>
#include <pthread.h>

#include "tagger.h"

#include "shared/blockingqueue.h"
#include "shared/buffpack.h"
#include "shared/messagetype.h"
#include "shared/threadlocal.h"

#include "objecttag.h"
#include "pbmanager.h"
#include "sender.h"

#include "../src-disl-agent/jvmtiutil.h"


static JavaVM * java_vm;
static jvmtiEnv * jvmti_env;
static jrawMonitorID tagging_lock;

static volatile jclass THREAD_CLASS = NULL;
static volatile jclass STRING_CLASS = NULL;
static volatile int no_tagging_work = 0;
static blocking_queue objtag_q;

// ******************* Object tagging thread *******************

// TODO add cache - ??

static void ot_pack_string_data(JNIEnv * jni_env, buffer * buff,
    jstring to_send, jlong str_net_ref) {

  // get string length
  jsize str_len = (*jni_env)->GetStringUTFLength(jni_env, to_send);

  // get string data as utf-8
  const char * str = (*jni_env)->GetStringUTFChars(jni_env, to_send, NULL);
  check_error(str == NULL, "Cannot get string from java");

  // check if the size is sendable
  int size_fits = str_len < UINT16_MAX;
  check_error(!size_fits, "Java string is too big for sending");

  // add message to the buffer
  messager_stringinfo_header(buff, str_net_ref, str, str_len);

  // release string
  (*jni_env)->ReleaseStringUTFChars(jni_env, to_send, str);
}

static void ot_pack_thread_data(JNIEnv * jni_env, buffer * buff,
    jstring to_send, jlong thr_net_ref) {

  jvmtiThreadInfo info;
  jvmtiError error = (*jvmti_env)->GetThreadInfo(jvmti_env, to_send, &info);
  check_error(error != JVMTI_ERROR_NONE, "Cannot get tread info");

  // pack thread info message
  messager_threadinfo_header(buff, thr_net_ref, info.name, strlen(info.name),
      info.is_daemon);
}

static void update_send_status(jobject to_send, jlong * net_ref) {

  net_ref_set_spec(net_ref, 1);
  update_net_reference(jvmti_env, to_send, *net_ref);
}

static void ot_pack_aditional_data(JNIEnv * jni_env, jlong * net_ref,
    jobject to_send, unsigned char obj_type, buffer * new_objs_buff) {

  // NOTE: we don't use lock for updating send status, so it is possible
  // that multiple threads will send it, but this will hurt only performance

  // test if the data was already sent to the server
  if (net_ref_get_spec(*net_ref) == 1) {
    return;
  }

  // NOTE: Tests for class types could be done by buffering threads.
  //       It depends, where we want to have the load.

  // String - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, STRING_CLASS)) {

    update_send_status(to_send, net_ref);
    ot_pack_string_data(jni_env, new_objs_buff, to_send, *net_ref);
  }

  // Thread - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, THREAD_CLASS)) {

    update_send_status(to_send, net_ref);
    ot_pack_thread_data(jni_env, new_objs_buff, to_send, *net_ref);
  }
}

static void ot_tag_record(JNIEnv * jni_env, buffer * buff, size_t buff_pos,
    jobject to_send, unsigned char obj_type, buffer * new_objs_buff) {

  // get net reference
  jlong net_ref = get_net_reference(jni_env, jvmti_env, new_objs_buff, to_send);

  // send additional data
  if (obj_type == OT_DATA_OBJECT) {

    // NOTE: can update net reference (net_ref)
    ot_pack_aditional_data(jni_env, &net_ref, to_send, obj_type, new_objs_buff);
  }

  // update the net reference
  buff_put_long(buff, buff_pos, net_ref);
}

static void ot_tag_buff(JNIEnv * jni_env, buffer * anl_buff, buffer * cmd_buff,
    buffer * new_objs_buff) {

  size_t cmd_buff_len = buffer_filled(cmd_buff);
  size_t read = 0;

  objtag_rec ot_rec;

  while (read < cmd_buff_len) {

    // read ot_rec data
    buffer_read(cmd_buff, read, &ot_rec, sizeof(ot_rec));
    read += sizeof(ot_rec);

    ot_tag_record(jni_env, anl_buff, ot_rec.buff_pos, ot_rec.obj_to_tag,
        ot_rec.obj_type, new_objs_buff);

    // global references are released after buffer is send
  }
}

// TODO code dup with ot_tag_buff
static void ot_relese_global_ref(JNIEnv * jni_env, buffer * cmd_buff) {

  size_t cmd_buff_len = buffer_filled(cmd_buff);
  size_t read = 0;

  objtag_rec ot_rec;

  while (read < cmd_buff_len) {

    // read ot_rec data
    buffer_read(cmd_buff, read, &ot_rec, sizeof(ot_rec));
    read += sizeof(ot_rec);

    // release global references
    (*jni_env)->DeleteGlobalRef(jni_env, ot_rec.obj_to_tag);
  }
}

static void * tagger_loop(void * obj) {

  // attach thread to jvm
  JNIEnv *jni_env;
  jvmtiError error = (*java_vm)->AttachCurrentThreadAsDaemon(java_vm,
      (void **) &jni_env, NULL);
  check_jvmti_error(jvmti_env, error, "Unable to attach objtag thread.");

  // one spare buffer for new objects
  buffer * new_obj_buff = malloc(sizeof(buffer));
  buffer_alloc(new_obj_buff);

  // retrieve java types

  if (STRING_CLASS == NULL) {
    STRING_CLASS = (*jni_env)->FindClass(jni_env, "java/lang/String");
    check_error(STRING_CLASS == NULL, "String class not found");
  }

  if (THREAD_CLASS == NULL) {
    THREAD_CLASS = (*jni_env)->FindClass(jni_env, "java/lang/Thread");
    check_error(STRING_CLASS == NULL, "Thread class not found");
  }

  // exit when the jvm is terminated and there are no msg to process
  while (!(no_tagging_work && bq_length(&objtag_q) == 0)) {

    // get buffer - before tagging lock
    process_buffs * pb;
    bq_pop(&objtag_q, &pb);

    // tag the objects - with lock
    enter_critical_section(jvmti_env, tagging_lock);
    {
      // tag objcects from buffer
      // note that analysis buffer is not required
      ot_tag_buff(jni_env, pb->analysis_buff, pb->command_buff, new_obj_buff);

      // exchange command_buff and new_obj_buff
      buffer * old_cmd_buff = pb->command_buff;
      pb->command_buff = new_obj_buff;

      // send buffer
      sender_enqueue(pb);

      // global references are released after buffer is send
      // this is critical for ensuring that proper ordering of events
      // is maintained - see object free event for more info

      ot_relese_global_ref(jni_env, old_cmd_buff);

      // clean old_cmd_buff and make it as new_obj_buff for the next round
      buffer_clean(old_cmd_buff);
      new_obj_buff = old_cmd_buff;
    }
    exit_critical_section(jvmti_env, tagging_lock);
  }

  buffer_free(new_obj_buff);
  free(new_obj_buff);
  new_obj_buff = NULL;

  return NULL;
}

void tagger_init(JavaVM * jvm, jvmtiEnv * env) {
  java_vm = jvm;
  jvmti_env = env;

  jvmtiError error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "object tags",
      &tagging_lock);
  check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");

  bq_create(&objtag_q, BQ_BUFFERS, sizeof(process_buffs *));
}

void tagger_connect(pthread_t *objtag_thread) {
  int res = pthread_create(objtag_thread, NULL, tagger_loop, NULL);
  check_error(res != 0, "Cannot create tagging thread");
}

void tagger_disconnect(pthread_t *objtag_thread, int size) {
  no_tagging_work = 1;

  // send empty buff to obj_tag thread -> ensures exit if waiting
  for (int i = 0; i < size; i++) {
    process_buffs * buffs = pb_normal_get(tld_get()->id);
    tagger_enqueue(buffs);
  }

  for (int i = 0; i < size; i++) {
    int res = pthread_join(objtag_thread[i], NULL);
    check_error(res != 0, "Cannot join tagging thread.");
  }
}

void tagger_enqueue(process_buffs * buffs) {
  buffs->owner_id = PB_OBJTAG;
  bq_push(&objtag_q, &buffs);
}

void tagger_newclass(JNIEnv* jni_env, jvmtiEnv *jvmti_env, jobject loader,
    const char* name, jint class_data_len, const unsigned char* class_data,
    int jvm_started) {
  // retrieve class loader net ref
  jlong loader_id = NULL_TAG;

  // obtain buffer
  process_buffs * buffs = pb_utility_get();
  buffer * buff = buffs->analysis_buff;

  // this callback can be called before the jvm is started
  // the loaded classes are mostly java.lang.*
  // classes will be (hopefully) loaded by the same class loader
  // this phase is indicated by NULL_TAG in the class loader id and it
  // is then handled by server
  if (jvm_started) {
    // tag the class loader - with lock
    enter_critical_section(jvmti_env, tagging_lock);
    {
      loader_id = get_net_reference(jni_env, jvmti_env, buffs->command_buff,
          loader);
    }
    exit_critical_section(jvmti_env, tagging_lock);
  }

  messager_newclass_header(buff, name, loader_id, class_data_len, class_data);
  // send message
  sender_enqueue(buffs);
}
