#ifndef _SENDER_H_
#define _SENDER_H_

#include <pthread.h>

#include "shared/buffer.h"

void sender_init(char *options);
void sender_connect(pthread_t *sender_thread);
void sender_disconnect(pthread_t *sender_thread, int size);
void sender_enqueue(process_buffs * buffs);

#endif /* _SENDER_H_ */
