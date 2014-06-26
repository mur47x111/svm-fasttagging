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

static void ot_pack_aditional_data(JNIEnv * jni_env, jlong * net_ref,
    jobject to_send, buffer * new_objs_buff) {
  // NOTE: we don't use lock for updating send status, so it is possible
  // that multiple threads will send it, but this will hurt only performance

  // NOTE: Tests for class types could be done by buffering threads.
  //       It depends, where we want to have the load.

  // String - pack data
  if (STRING_CLASS == NULL) {
    STRING_CLASS = (*jni_env)->FindClass(jni_env, "java/lang/String");
    check_error(STRING_CLASS == NULL, "String class not found");
  }

  if (THREAD_CLASS == NULL) {
    THREAD_CLASS = (*jni_env)->FindClass(jni_env, "java/lang/Thread");
    check_error(STRING_CLASS == NULL, "Thread class not found");
  }

  if ((*jni_env)->IsInstanceOf(jni_env, to_send, STRING_CLASS)) {
    ot_set_spec(to_send, *net_ref);
    ot_pack_string_data(jni_env, new_objs_buff, to_send, *net_ref);
  }

  // Thread - pack data
  if ((*jni_env)->IsInstanceOf(jni_env, to_send, THREAD_CLASS)) {
    ot_set_spec(to_send, *net_ref);
    ot_pack_thread_data(jni_env, new_objs_buff, to_send, *net_ref);
  }
}


static void pack_object(JNIEnv * jni_env, buffer * buff, buffer * cmd_buff,
    jobject to_send, bool sent_data) {
  // pack null net reference
  jlong tag = ot_get_tag(jni_env, to_send);
  pack_long(buff, tag);

  if (sent_data && !ot_is_spec_set(tag)) {
    ot_pack_aditional_data(jni_env, &tag, to_send, cmd_buff);
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
  return register_method(jni_env, analysis_method_desc, tld_get()->id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__S(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {
  tl_insert_analysis_item(analysis_method_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jbyte ordering_id) {
  tl_insert_analysis_item_ordering(analysis_method_id, ordering_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisEnd(
    JNIEnv * jni_env, jclass this_class) {
  tl_analysis_end();
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
  pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send, 0);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {
  tldata * tld = tld_get();
  pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send, 1);
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
}
