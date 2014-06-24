#ifndef _TAGGER_H_
#define _TAGGER_H_

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
void tagger_connect();
void tagger_disconnect();
void tagger_enqueue(process_buffs * buffs);

void tagger_jvmstart();
void tagger_newclass(JNIEnv* jni_env, jvmtiEnv *jvmti_env, jobject loader,
    const char* name, jint class_data_len, const unsigned char* class_data);

#endif /* _TAGGER_H_ */
