#ifndef _REDISPATCHER_H
#define	_REDISPATCHER_H

#include <jvmti.h>

void redispatcher_register_natives(JNIEnv * jni_env, jclass klass);

void redispatcher_init();

void redispatcher_object_free(jlong tag);
void redispatcher_thread_end();
void redispatcher_vm_death();

#endif	/* _REDISPATCHER_H */
