#include "redispatcher.h"

#include "shared/buffpack.h"
#include "shared/threadlocal.h"

#include "tlocalbuffer.h"

// ******************* REDispatch methods *******************

JNIEXPORT jshort JNICALL Java_ch_usi_dag_dislre_REDispatch_registerMethod(
    JNIEnv * jni_env, jclass this_class, jstring analysis_method_desc) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  return tl_register_method(jni_env, analysis_method_desc, tld_get()->id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__S(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_insert_analysis_item(analysis_method_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jbyte ordering_id) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_insert_analysis_item_ordering(analysis_method_id, ordering_id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisEnd(
    JNIEnv * jni_env, jclass this_class) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_analysis_end();
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendBoolean(
    JNIEnv * jni_env, jclass this_class, jboolean to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_boolean(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendByte(
    JNIEnv * jni_env, jclass this_class, jbyte to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_byte(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendChar(
    JNIEnv * jni_env, jclass this_class, jchar to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_char(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendShort(
    JNIEnv * jni_env, jclass this_class, jshort to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_short(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendInt(
    JNIEnv * jni_env, jclass this_class, jint to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_int(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendLong(
    JNIEnv * jni_env, jclass this_class, jlong to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_long(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendFloat(
    JNIEnv * jni_env, jclass this_class, jfloat to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_float(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendDouble(
    JNIEnv * jni_env, jclass this_class, jdouble to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  pack_double(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObject(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_pack_object(jni_env, tld_get()->analysis_buff, to_send, 0);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData(
    JNIEnv * jni_env, jclass this_class, jobject to_send) {

#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_pack_object(jni_env, tld_get()->analysis_buff, to_send, 1);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analyze(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {
#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_insert_analysis_item(analysis_method_id);
  tl_analysis_end();
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analyzeO(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jobject to_send) {
#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tl_insert_analysis_item(analysis_method_id);
  tl_pack_object(jni_env, tld_get()->analysis_buff, to_send, 0);
  tl_analysis_end();
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analyzeOD(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jobject to_send1, jobject to_send2) {
#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tldata *tld = tld_get();

  tl_insert_analysis_item(analysis_method_id);
  tl_pack_object(jni_env, tld->analysis_buff, to_send1, 0);
  tl_pack_object(jni_env, tld->analysis_buff, to_send2, 1);
  tl_analysis_end();
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analyzeODD(
    JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
    jobject to_send1, jobject to_send2, jobject to_send3) {
#ifdef DEBUGMETRICS
  tl_increase_all_counter();
#endif

  tldata *tld = tld_get();

  tl_insert_analysis_item(analysis_method_id);
  tl_pack_object(jni_env, tld->analysis_buff, to_send1, 0);
  tl_pack_object(jni_env, tld->analysis_buff, to_send2, 1);
  tl_pack_object(jni_env, tld->analysis_buff, to_send3, 1);
  tl_analysis_end();
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
    {"analyze",            "(S)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_analyze},
    {"analyzeO",           "(SLjava/lang/Object;)V",(void *)&Java_ch_usi_dag_dislre_REDispatch_analyzeO},
    {"analyzeOD",          "(SLjava/lang/Object;Ljava/lang/Object;)V",
                                                    (void *)&Java_ch_usi_dag_dislre_REDispatch_analyzeOD},
    {"analyzeODD",         "(SLjava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V",
                                                    (void *)&Java_ch_usi_dag_dislre_REDispatch_analyzeODD},
};

void redispatcher_register_natives(JNIEnv * jni_env, jvmtiEnv * jvmti,
    jclass klass) {
  (*jni_env)->RegisterNatives(jni_env, klass, redispatchMethods,
      sizeof(redispatchMethods) / sizeof(redispatchMethods[0]));
}
