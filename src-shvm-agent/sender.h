#ifndef _SENDER_H_
#define _SENDER_H_

#include "shared/buffer.h"

void sender_init(char *options);
void sender_connect();
void sender_disconnect();
void sender_enqueue(process_buffs * buffs);

#endif /* _SENDER_H_ */
