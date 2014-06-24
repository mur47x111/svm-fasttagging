#ifndef _REDISPATCHER_H
#define	_REDISPATCHER_H

#include <jvmti.h>

void redispatcher_register_natives(JNIEnv * jni_env, jclass klass);

#endif	/* _REDISPATCHER_H */
