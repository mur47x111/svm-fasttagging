#ifndef _SENDER_H_
#define _SENDER_H_

#include <pthread.h>

#include "shared/buffer.h"

void sender_init(char *options);
void sender_connect(pthread_t *sender_thread, int size);
void sender_disconnect(pthread_t *sender_thread, int size);
void sender_enqueue(process_buffs * buffs);

void sender_newclass(const char* name, jlong loader_id, jint class_data_len,
    const unsigned char* class_data);
void sender_classinfo(jlong tag, const char* class_sig, const char* class_gen,
    jlong class_loader_tag, jlong super_class_tag);

#endif /* _SENDER_H_ */
