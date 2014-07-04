#ifndef _TLOCALBUFFER_H_
#define _TLOCALBUFFER_H_

#include <stdbool.h>

#include <jni.h>
#include <jvmti.h>

#include "shared/buffer.h"

#include "globalbuffer.h"

#define STARTING_THREAD_ID (TO_BUFFER_MAX_ID + 1)

jshort tl_register_method(JNIEnv * jni_env, jstring analysis_method_desc,
    jlong thread_id);

void tl_insert_analysis_item(jshort analysis_method_id);
void tl_insert_analysis_item_ordering(jshort analysis_method_id,
    jbyte ordering_id);
void tl_analysis_end();

void tl_send_buffer();
void tl_thread_end();

#ifdef DEBUGMETRICS
void tl_increase_start_counter();
void tl_increase_all_counter();
#endif

void tl_pack_object(JNIEnv * jni_env, buffer * buff, jobject to_send,
    bool sent_data);
void tl_init(JNIEnv * jni_env, jvmtiEnv * jvmti);
void tl_print_counters();

#endif /* _TLOCALBUFFER_H_ */
