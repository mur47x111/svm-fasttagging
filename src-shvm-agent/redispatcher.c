#include "redispatcher.h"

#include "shared/buffpack.h"
#include "shared/messagetype.h"
#include "shared/threadlocal.h"

#include "objecttag.h"
#include "tlocalbuffer.h"
#include "pbmanager.h"
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

static inline void increase_start_counter() {
  __sync_fetch_and_add(&jni_start_counter, 1);
}

static inline void increase_all_counter() {
  __sync_fetch_and_add(&jni_all_counter, 1);
}

#endif

// ******************* Object tagging thread *******************

// TODO add cache - ??

static void ot_pack_string_data(JNIEnv * jni_env, jstring to_send,
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

static void ot_pack_thread_data(JNIEnv * jni_env, jstring to_send,
    jlong thr_tag) {

  jvmtiThreadInfo info;
  jvmtiError error = (*jvmti_env)->GetThreadInfo(jvmti_env, to_send, &info);
  check_error(error != JVMTI_ERROR_NONE, "Cannot get tread info");

  // pack thread info message
  sender_threadinfo(thr_tag, info.name, strlen(info.name), info.is_daemon);
}

static void ot_pack_aditional_data(JNIEnv * jni_env, jlong * net_ref,
    jobject to_send) {
  // NOTE: we don't use lock for updating send status, so it is possible
  // that multiple threads will send it, but this will hurt only performance

  // NOTE: Tests for class types could be done by buffering threads.
  //       It depends, where we want to have the load.
  // String - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, STRING_CLASS)) {
    ot_pack_string_data(jni_env, to_send, *net_ref);
    ot_set_spec(to_send, *net_ref);
  }

  // Thread - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, THREAD_CLASS)) {
    ot_pack_thread_data(jni_env, to_send, *net_ref);
    ot_set_spec(to_send, *net_ref);
  }
}

static void pack_object(JNIEnv * jni_env, buffer * buff, jobject to_send,
    bool sent_data) {
  // pack null net reference
  jlong tag = ot_get_tag(jni_env, to_send);
  pack_long(buff, tag);

  if (sent_data && !ot_is_spec_set(tag)) {
    ot_pack_aditional_data(jni_env, &tag, to_send);
  }
}

// ******************* analysis helper methods *******************

// first available id for new messages
static volatile jshort avail_analysis_id = 1;

static inline jlong next_analysis_id() {
  return __sync_fetch_and_add(&avail_analysis_id, 1);
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

// ******************* REDispatch methods *******************

JNIEXPORT jshort JNICALL Java_ch_usi_dag_dislre_REDispatch_registerMethod(
    JNIEnv * jni_env, jclass this_class, jstring analysis_method_desc) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  return register_method(jni_env, analysis_method_desc, tld_get()->id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__S(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {

#ifdef DEBUGMETRICS
  increase_all_counter();
  increase_start_counter();
#endif

  tl_insert_analysis_item(analysis_method_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jbyte ordering_id) {

#ifdef DEBUGMETRICS
  increase_all_counter();
  increase_start_counter();
#endif

  tl_insert_analysis_item_ordering(analysis_method_id, ordering_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisEnd(
    JNIEnv * jni_env, jclass this_class) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  tl_analysis_end();
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendBoolean(
    JNIEnv * jni_env, jclass this_class, jboolean to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_boolean(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendByte(
    JNIEnv * jni_env, jclass this_class, jbyte to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_byte(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendChar(
    JNIEnv * jni_env, jclass this_class, jchar to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_char(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendShort(
    JNIEnv * jni_env, jclass this_class, jshort to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_short(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendInt(
    JNIEnv * jni_env, jclass this_class, jint to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_int(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendLong(
    JNIEnv * jni_env, jclass this_class, jlong to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_long(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendFloat(
    JNIEnv * jni_env, jclass this_class, jfloat to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_float(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendDouble(
    JNIEnv * jni_env, jclass this_class, jdouble to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  pack_double(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObject(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  tldata * tld = tld_get();
  pack_object(jni_env, tld->analysis_buff, to_send, 0);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {

#ifdef DEBUGMETRICS
  increase_all_counter();
#endif

  tldata * tld = tld_get();
  pack_object(jni_env, tld->analysis_buff, to_send, 1);
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

void redispatcher_register_natives(JNIEnv * jni_env, jvmtiEnv * jvmti,
    jclass klass) {
  (*jni_env)->RegisterNatives(jni_env, klass, redispatchMethods,
      sizeof(redispatchMethods) / sizeof(redispatchMethods[0]));

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

void redispatcher_print_counters() {
#ifdef DEBUGMETRICS
  printf("TOTAL JNI CALL: %ld\n", jni_all_counter);
  printf("TOTAL ANALYSIS START CALL: %ld\n", jni_start_counter);
#endif
}
