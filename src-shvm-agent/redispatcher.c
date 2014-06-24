#include "redispatcher.h"

#include "shared/buffpack.h"
#include "shared/messagetype.h"
#include "shared/threadlocal.h"

#include "netref.h"
#include "globalbuffer.h"
#include "pbmanager.h"
#include "sender.h"
#include "tagger.h"
#include "freehandler.h"

#include "../src-disl-agent/jvmtiutil.h"

// number of analysis requests in one message
#define ANALYSIS_COUNT 16384

// first available id for new messages
static volatile jshort avail_analysis_id = 1;

// initial ids are reserved for total ordering buffers
static volatile jlong avail_thread_id = STARTING_THREAD_ID;

// ******************* Advanced packing routines *******************

static void _fill_ot_rec(JNIEnv * jni_env, buffer * cmd_buff,
    unsigned char ot_type, buffer * buff, jstring to_send) {

  // crate object tagging record
  objtag_rec ot_rec;
  // type of object to be tagged
  ot_rec.obj_type = ot_type;
  // position in the buffer, where the data will be stored during tagging
  ot_rec.buff_pos = buffer_filled(buff);
  // global reference to the object to be tagged
  ot_rec.obj_to_tag = (*jni_env)->NewGlobalRef(jni_env, to_send);

  // save to command buff
  buffer_fill(cmd_buff, &ot_rec, sizeof(ot_rec));
}

static void pack_object(JNIEnv * jni_env, buffer * buff, buffer * cmd_buff,
    jobject to_send, unsigned char object_type) {

  // create entry for object tagging thread that will replace the null ref
  if (to_send != NULL) {
    _fill_ot_rec(jni_env, cmd_buff, object_type, buff, to_send);
  }

  // pack null net reference
  pack_long(buff, NULL_NET_REF);
}

// ******************* analysis helper methods *******************

static jvmtiEnv * jvmti_env;

static jrawMonitorID obj_free_lock;

static jshort next_analysis_id() {
  // get id for this method string
  // this could use different lock then tagging but it should not be a problem
  // and it will be used rarely - bit unoptimized

  jshort result = -1;
  enter_critical_section(jvmti_env, obj_free_lock);
  {
    result = avail_analysis_id++;
  }
  exit_critical_section(jvmti_env, obj_free_lock);

  return result;
}

static jlong next_thread_id() {
  // mark the thread - with lock
  // TODO replace total ordering lock with private lock - perf. issue
  jlong result = -1;
  enter_critical_section(jvmti_env, obj_free_lock);
  {
    result = avail_thread_id++;
  }
  exit_critical_section(jvmti_env, obj_free_lock);
  return result;
}

static jshort register_method(JNIEnv * jni_env, jstring analysis_method_desc,
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

static void analysis_start(JNIEnv * jni_env, jshort analysis_method_id,
    tldata * tld) {
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

static void analysis_end(tldata * tld) {
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


static void analysis_start_buff(JNIEnv * jni_env, jshort analysis_method_id,
    jbyte ordering_id, tldata * tld) {
  check_error(ordering_id < 0, "Buffer id has negative value");

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

static void analysis_end_switch(tldata * tld) {
  // update the length of the marshalled arguments
  jshort args_length = buffer_filled(tld->analysis_buff) - tld->args_length_pos
      - sizeof(jshort);
  buff_put_short(tld->analysis_buff, tld->args_length_pos, args_length);

  // this method is also called for end of analysis for totally ordered API
  if (tld->to_buff_id != INVALID_BUFF_ID) {
    // TODO lock for each buffer id
    // sending of half-full buffer is done in shutdown hook and obj free hook
    // write analysis to total order buffer - with lock
    glbuffer_copy_from_tlbuffer();

    // reset analysis and command buffers for normal buffering
    // set to NULL, because we've send the buffers at the beginning of
    // global buffer buffering
    tld->analysis_buff = NULL;
    tld->command_buff = NULL;

    // invalidate buffer id
    tld->to_buff_id = INVALID_BUFF_ID;
  } else {
    analysis_end(tld);
  }
}

// ******************* REDispatch methods *******************

JNIEXPORT jshort JNICALL Java_ch_usi_dag_dislre_REDispatch_registerMethod(
    JNIEnv * jni_env, jclass this_class, jstring analysis_method_desc) {
  return register_method(jni_env, analysis_method_desc, tld_get()->id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__S(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {
  analysis_start(jni_env, analysis_method_id, tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jbyte ordering_id) {
  analysis_start_buff(jni_env, analysis_method_id, ordering_id, tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisEnd(
    JNIEnv * jni_env, jclass this_class) {
  analysis_end_switch(tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendBoolean(
    JNIEnv * jni_env, jclass this_class, jboolean to_send) {
  pack_boolean(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendByte(
    JNIEnv * jni_env, jclass this_class, jbyte to_send) {
  pack_byte(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendChar(
    JNIEnv * jni_env, jclass this_class, jchar to_send) {
  pack_char(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendShort(
    JNIEnv * jni_env, jclass this_class, jshort to_send) {
  pack_short(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendInt(
    JNIEnv * jni_env, jclass this_class, jint to_send) {
  pack_int(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendLong(
    JNIEnv * jni_env, jclass this_class, jlong to_send) {
  pack_long(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendFloat(
    JNIEnv * jni_env, jclass this_class, jfloat to_send) {
  pack_float(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendDouble(
    JNIEnv * jni_env, jclass this_class, jdouble to_send) {
  pack_double(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObject(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {
  tldata * tld = tld_get();
  pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send,
  OT_OBJECT);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {
  tldata * tld = tld_get();
  pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send,
  OT_DATA_OBJECT);
}

static JNINativeMethod redispatchMethods[] = {
    {"registerMethod",     "(Ljava/lang/String;)S", (void *)&Java_ch_usi_dag_dislre_REDispatch_registerMethod},
    {"analysisStart",      "(S)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisStart__S},
    {"analysisStart",      "(SB)V",                 (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB},
    {"analysisEnd",        "()V",                   (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisEnd},
    {"sendBoolean",        "(Z)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendBoolean},
    {"sendByte",           "(B)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendByte},
    {"sendChar",           "(C)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendChar},
    {"sendShort",          "(S)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendShort},
    {"sendInt",            "(I)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendInt},
    {"sendLong",           "(J)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendLong},
    {"sendFloat",          "(F)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendFloat},
    {"sendDouble",         "(D)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendDouble},
    {"sendObject",         "(Ljava/lang/Object;)V", (void *)&Java_ch_usi_dag_dislre_REDispatch_sendObject},
    {"sendObjectPlusData", "(Ljava/lang/Object;)V", (void *)&Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData},
};

void redispatcher_init(jvmtiEnv *env) {
  jvmti_env = env;

  jvmtiError error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "obj free",
      &obj_free_lock);
  check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");
}

void redispatcher_register_natives(JNIEnv * jni_env, jclass klass) {
  (*jni_env)->RegisterNatives(jni_env, klass, redispatchMethods,
      sizeof(redispatchMethods) / sizeof(redispatchMethods[0]));
}

void redispatcher_object_free(jlong tag) {
  fh_object_free(tag);
}

static void send_thread_buffers(tldata * tld) {
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

void redispatcher_thread_end() {
  // It should be safe to use thread locals according to jvmti documentation:
  // Thread end events are generated by a terminating thread after its initial
  // method has finished execution.
  jlong thread_id = tld_get()->id;

  if (thread_id == INVALID_THREAD_ID) {
    return;
  }

  // send all pending buffers associated with this thread
  send_thread_buffers(tld_get());

  // send thread end message
  process_buffs * buffs = pb_normal_get(thread_id);
  messager_threadend_header(buffs->analysis_buff, thread_id);

  // send to object tagging queue - this thread could have something still
  // in the queue so we ensure proper ordering
  tagger_enqueue(buffs);
}

void redispatcher_vm_death() {
  tldata * tld = tld_get();

  glbuffer_sendall();

  // send buffers of shutdown thread
  send_thread_buffers(tld);

  // send object free buffer
  fh_send_buffer();
}
