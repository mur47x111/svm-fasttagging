#ifndef _FREEHANDLER_H_
#define _FREEHANDLER_H_

#include <jvmti.h>

void fh_init(jvmtiEnv *env);
void fh_object_free(jlong tag);
void fh_send_buffer();


#endif /* _FREEHANDLER_H_ */
