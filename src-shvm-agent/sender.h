#ifndef _SVMCONNECTOR_H_
#define _SVMCONNECTOR_H_

#include "processbuffs.h"

void sender_init(char *options);
void sender_connect();
void sender_disconnect();
void sender_enqueue(process_buffs * buffs);

#endif /* _SVMCONNECTOR_H_ */
