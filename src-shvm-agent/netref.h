#ifndef _NETREF_H
#define	_NETREF_H

#include <jvmti.h>
#include <jni.h>

#include "buffer.h"

#define NULL_NET_REF 0

// ******************* Net reference routines *******************

unsigned char net_ref_get_spec(jlong net_ref);

void net_ref_set_spec(jlong * net_ref, unsigned char spec);

// only retrieves object tag data
jlong get_tag(jvmtiEnv * jvmti_env, jobject obj);

// retrieves net_reference - performs tagging if necessary
// can be used for any object - even classes
// !!! invocation of this method should be protected by lock until the reference
// is queued for sending
jlong get_net_reference(JNIEnv * jni_env, jvmtiEnv * jvmti_env,
		buffer * new_obj_buff, jobject obj);

// !!! invocation of this method should be protected by lock until the reference
// is queued for sending
void update_net_reference(jvmtiEnv * jvmti_env, jobject obj, jlong net_ref);

#endif	/* _NETREF_H */
