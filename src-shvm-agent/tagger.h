#ifndef _TAGGER_H_
#define _TAGGER_H_

#include <pthread.h>

#include <jni.h>
#include <jvmti.h>

#include "shared/buffer.h"

#define OT_OBJECT 1
#define OT_DATA_OBJECT 2

typedef struct {
  unsigned char obj_type;
  size_t buff_pos;
  jobject obj_to_tag;
} objtag_rec;

void tagger_init(JavaVM * jvm, jvmtiEnv * env);
void tagger_connect(pthread_t *objtag_thread);
void tagger_disconnect(pthread_t *objtag_thread, int size);
void tagger_enqueue(process_buffs * buffs);

void tagger_newclass(JNIEnv* jni_env, jvmtiEnv *jvmti_env, jobject loader,
    const char* name, jint class_data_len, const unsigned char* class_data,
    int jvm_started);

#endif /* _TAGGER_H_ */
