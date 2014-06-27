#ifndef _SENDER_H_
#define _SENDER_H_

#include <pthread.h>

#include "shared/buffer.h"

void sender_init(char *options);
void sender_connect(pthread_t *sender_thread, int size);
void sender_disconnect(pthread_t *sender_thread, int size);
void sender_enqueue(process_buffs * buffs);

void sender_newclass(const char* name, jsize name_len, jlong loader_id,
		jint class_data_len, const unsigned char* class_data);
void sender_classinfo(jlong tag, const char* class_sig, jsize class_sig_len,
		const char* class_gen, jsize class_gen_len, jlong class_loader_tag,
		jlong super_class_tag);

void sender_stringinfo(jlong str_tag, const char * str, jsize str_len);
void sender_threadinfo(jlong thread_tag, const char *str, jsize str_len,
    jboolean is_daemon);

#endif /* _SENDER_H_ */
