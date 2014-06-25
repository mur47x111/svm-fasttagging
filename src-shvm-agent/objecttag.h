#ifndef _NETREF_H
#define	_NETREF_H

#include <jvmti.h>
#include <jni.h>

#include "shared/buffer.h"

#define NULL_TAG 0

// ******************* Net reference routines *******************

void ot_init(jvmtiEnv * env);
jlong ot_get_tag(JNIEnv * jni_env, jobject obj);
int ot_is_spec_set(jlong tag);
void ot_set_spec(jobject obj, jlong tag);

#endif	/* _NETREF_H */
