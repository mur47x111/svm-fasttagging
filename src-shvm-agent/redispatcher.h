#ifndef _REDISPATCHER_H
#define	_REDISPATCHER_H

#include <jvmti.h>

void redispatcher_register_natives(JNIEnv * jni_env, jvmtiEnv * jvmti,
    jclass klass);
void redispatcher_print_counters();

#endif	/* _REDISPATCHER_H */
