#include <stdint.h>
#include <string.h>

#include "objecttag.h"

#include "shared/buffpack.h"
#include "shared/messagetype.h"

#include "sender.h"
#include "pbmanager.h"

#include "../src-disl-agent/jvmtiutil.h"

// first available object id
static volatile jlong avail_object_id = 1;

// first available class id
static volatile jint avail_class_id = 1;

static inline jlong next_object_id() {
  return __sync_fetch_and_add(&avail_object_id, 1);
}

static inline jlong next_class_id() {
  return __sync_fetch_and_add(&avail_class_id, 1);
}

// ******************* get/set tag routines *******************

// format of net reference looks like this (from HIGHEST)
// 1 bit data trans., 1 bit class instance, 23 bits class id, 40 bits object id
// bit field not used because there is no guarantee of alignment

// TODO rename SPEC
// SPEC flag is used to indicate if some additional data for this object where
// transfered to the server

#define OBJECT_ID_POS       (uint8_t)0
#define CLASS_ID_POS        (uint8_t)40
#define CLASS_INSTANCE_POS  (uint8_t)62
#define SPEC_POS            (uint8_t)63

#define OBJECT_ID_MASK      0xFFFFFFFFFFL
#define CLASS_ID_MASK       0x3FFFFFL
#define CLASS_INSTANCE_MASK 0x1L
#define SPEC_MASK           0x1L

// get bits from "from" with pattern "bit_mask" lowest bit starting on position
// "low_start" (from 0)
static inline uint64_t get_bits(uint64_t from, uint64_t bit_mask,
    uint8_t low_start) {
  uint64_t bits_shifted = from >> low_start;
  return bits_shifted & bit_mask;
}

// set bits "bits" to "to" with pattern "bit_mask" lowest bit starting on
// position "low_start" (from 0)
static inline void set_bits(uint64_t * to, uint64_t bits, uint64_t bit_mask,
    uint8_t low_start) {
  uint64_t bits_len = bits & bit_mask;
  uint64_t bits_pos = bits_len << low_start;
  *to |= bits_pos;
}

static inline jlong fold_tag(jlong object_id, jint class_id, unsigned char spec,
    unsigned char cbit) {
  jlong tag = 0;

  set_bits((uint64_t *) &tag, object_id, OBJECT_ID_MASK, OBJECT_ID_POS);
  set_bits((uint64_t *) &tag, class_id, CLASS_ID_MASK, CLASS_ID_POS);
  set_bits((uint64_t *) &tag, spec, SPEC_MASK, SPEC_POS);
  set_bits((uint64_t *) &tag, cbit, CLASS_INSTANCE_MASK,
  CLASS_INSTANCE_POS);

  return tag;
}

static jvmtiEnv * jvmti_env;
static jrawMonitorID tagging_lock;

void ot_init(jvmtiEnv * env) {
  jvmti_env = env;

  jvmtiError error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "object tags",
      &tagging_lock);
  check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");
}

static void jvm_set_tag(jobject obj, jlong tag) {
  jvmtiError error = (*jvmti_env)->SetTag(jvmti_env, obj, tag);
  check_jvmti_error(jvmti_env, error, "Cannot set object tag");
}

#ifndef FASTTAGGING

// only retrieves object tag data
static jlong jvm_get_tag(jobject obj) {
  jlong tag;
  jvmtiError error = (*jvmti_env)->GetTag(jvmti_env, obj, &tag);
  check_jvmti_error(jvmti_env, error, "Cannot get object tag");
  return tag;
}

static int jvm_is_class(jobject obj) {
  // TODO isn't there better way?
  jvmtiError error = (*jvmti_env)->GetClassSignature(jvmti_env, obj, NULL,
  NULL);
  return error == JVMTI_ERROR_NONE;
}

static jlong jvm_get_object_class_tag(JNIEnv * jni_env, jobject obj) {
  jclass klass = (*jni_env)->GetObjectClass(jni_env, obj);
  jlong klass_tag = ot_get_tag(jni_env, klass);

  // free local reference
  (*jni_env)->DeleteLocalRef(jni_env, klass);
  return klass_tag;
}

static jlong jvm_set_tag_object(jobject obj, jlong temp_tag) {
  jlong tag = 0;

  enter_critical_section(jvmti_env, tagging_lock);
  {
    // compare and swap
    tag = jvm_get_tag(obj);

    if (tag == 0) {
      jvm_set_tag(obj, temp_tag);
      tag = temp_tag;
    }
  }
  exit_critical_section(jvmti_env, tagging_lock);

  return tag;
}

#else

typedef struct {
  jshort length;
  jshort padding_0;
  jint padding_1;
  char str;
} Symbol;

typedef struct _oopDesc oopDesc;

typedef struct {
  jlong padding_0;
  jlong padding_1;
  Symbol *name;
  jlong padding_2[10];
  oopDesc *java_mirror;
} Klass;

struct _oopDesc {
  jlong padding_0;
  Klass *klass;
  jlong tag;
};

static inline oopDesc * dereference(jobject obj) {
  return (*((oopDesc **) obj));
}

// only retrieves object tag data
static jlong jvm_get_tag(jobject obj) {
  return dereference(obj)->tag;
}

static int jvm_is_class(jobject obj) {
  Symbol *name = dereference(obj)->klass->name;
  return name->length == 15 && strncmp("java/lang/Class", &name->str, 15) == 0;
}

static jlong jvm_get_object_class_tag(JNIEnv * jni_env, jobject obj) {
  jclass klass = (jclass) & dereference(obj)->klass->java_mirror;
  return ot_get_tag(jni_env, klass);
}

static jlong jvm_set_tag_object(jobject obj, jlong temp_tag) {
  jlong tag = 0;

  jvmtiError error = (*jvmti_env)->CompareAndSwapTag(jvmti_env, obj, temp_tag,
      (jlong) 0, &tag);
  check_jvmti_error(jvmti_env, error, "CompareAndSwapTag failed");

  if (tag == 0) {
    tag = temp_tag;
  }

  return tag;
}

#endif

static jlong jvm_set_tag_class(JNIEnv * jni_env, jclass klass, jlong temp) {
  // manage references
  // http://docs.oracle.com/javase/6/docs/platform/jvmti/jvmti.html#refs
  static const jint ADD_REFS = 16;
  jint res = (*jni_env)->PushLocalFrame(jni_env, ADD_REFS);
  check_error(res != 0, "Cannot allocate more references");

  // *** pack class info into buffer ***
  jvmtiError error;

  // resolve descriptor + generic
  char * class_sig;
  char * class_gen;
  error = (*jvmti_env)->GetClassSignature(jvmti_env, klass, &class_sig,
      &class_gen);
  check_jvmti_error(jvmti_env, error, "Cannot get class signature");

  // get class loader tag
  jobject class_loader;
  error = (*jvmti_env)->GetClassLoader(jvmti_env, klass, &class_loader);
  check_jvmti_error(jvmti_env, error, "Cannot get class loader");
  jlong class_loader_tag = ot_get_tag(jni_env, class_loader);

  // get super class tag
  jclass super_class = (*jni_env)->GetSuperclass(jni_env, klass);
  jlong super_class_tag = ot_get_tag(jni_env, super_class);

  jlong tag = 0;

	// Assumption: no lock on the klass object
	res = (*jni_env)->MonitorEnter(jni_env, klass);
	check_error(res != 0, "monitor enter failed");
	{
		// compare and swap
		tag = jvm_get_tag(klass);

		if (tag == 0) {
			// pack class info into buffer
			if (class_gen == NULL) {
				sender_classinfo(temp, class_sig, strlen(class_sig), "", 0,
						class_loader_tag, super_class_tag);
			} else {
				sender_classinfo(temp, class_sig, strlen(class_sig), class_gen,
						strlen(class_gen), class_loader_tag, super_class_tag);
			}

			jvm_set_tag(klass, temp);
			tag = temp;
		}
	}
	res = (*jni_env)->MonitorExit(jni_env, klass);
	check_error(res != 0, "monitor exit failed");

  // deallocate memory
  error = (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *) class_sig);
  check_jvmti_error(jvmti_env, error, "Cannot deallocate memory");
  error = (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *) class_gen);
  check_jvmti_error(jvmti_env, error, "Cannot deallocate memory");

  // manage references - see function top
  (*jni_env)->PopLocalFrame(jni_env, NULL);

  return tag;
}

jlong ot_get_tag(JNIEnv * jni_env, jobject obj) {
  if (obj == NULL) { // net reference for NULL is 0
    return 0;
  }

  jlong tag = jvm_get_tag(obj);

  if (tag == 0) {
    jlong temp_tag = 0;

    if (jvm_is_class(obj)) {
      temp_tag = fold_tag(next_object_id(), next_class_id(), 1, 1);
      // class object should be handled separately
      // need to send class information
      tag = jvm_set_tag_class(jni_env, obj, temp_tag);
    } else {
      // get class of this object
      jlong klass_tag = jvm_get_object_class_tag(jni_env, obj);
      jint klass_id = get_bits(klass_tag, CLASS_ID_MASK, CLASS_ID_POS);

      temp_tag = fold_tag(next_object_id(), klass_id, 0, 0);
      tag = jvm_set_tag_object(obj, temp_tag);
    }
  }

  return tag;
}

int ot_is_spec_set(jlong tag) {
  return get_bits(tag, SPEC_MASK, SPEC_POS) == 1;
}

// is queued for sending
void ot_set_spec(jobject obj, jlong tag) {
  set_bits((uint64_t *) &tag, 1L, SPEC_MASK, SPEC_POS);
  jvm_set_tag(obj, tag);
}
