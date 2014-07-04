#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include <pthread.h>

#include <jvmti.h>
#include <jni.h>

#include "dislreagent.h"

#include "shared/threadlocal.h"

#include "pbmanager.h"
#include "redispatcher.h"
#include "sender.h"
#include "globalbuffer.h"
#include "tlocalbuffer.h"
#include "freehandler.h"
#include "objecttag.h"

#include "../src-disl-agent/jvmtiutil.h"

static pthread_t sender_thread;

static int jvm_started = 0;

void JNICALL jvmti_callback_class_file_load_hook(jvmtiEnv *jvmti_env,
    JNIEnv* jni_env, jclass class_being_redefined, jobject loader,
    const char* name, jobject protection_domain, jint class_data_len,
    const unsigned char* class_data, jint* new_class_data_len,
    unsigned char** new_class_data) {
  // retrieve class loader net ref
  jlong loader_id = NULL_TAG;

  // this callback can be called before the jvm is started
  // the loaded classes are mostly java.lang.*
  // classes will be (hopefully) loaded by the same class loader
  // this phase is indicated by NULL_TAG in the class loader id and it
  // is then handled by server
  if (jvm_started) {
    // tag the class loader - with lock
    loader_id = ot_get_tag(jni_env, loader);
  }

  sender_newclass(name, strlen(name), loader_id, class_data_len, class_data);
}

// registers all native methods so they can be used during VM init phase
void JNICALL jvmti_callback_class_prepare_hook(jvmtiEnv *jvmti_env,
    JNIEnv* jni_env, jthread thread, jclass klass) {
  static long registedFlag = 0;

  if (!registedFlag) {
    char * class_sig;
    jvmtiError error = (*jvmti_env)->GetClassSignature(jvmti_env, klass,
        &class_sig, NULL);
    check_jvmti_error(jvmti_env, error, "Cannot get class signature");

    if (strcmp(class_sig, "Lch/usi/dag/dislre/REDispatch;") == 0) {
      redispatcher_register_natives(jni_env, jvmti_env, klass);
      tl_init(jni_env, jvmti_env);
      registedFlag = 1;
    }

    // deallocate memory
    error = (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *) class_sig);
    check_jvmti_error(jvmti_env, error, "Cannot deallocate memory");
  }

  // The following line enables the early tagging of classes
  //  ot_get_tag(jni_env, klass);
}

void JNICALL jvmti_callback_vm_start_hook(jvmtiEnv *jvmti_env, JNIEnv* jni_env) {
  jvm_started = 1;
}

void JNICALL jvmti_callback_object_free_hook(jvmtiEnv *jvmti_env, jlong tag) {
  fh_object_free(tag);
}

void JNICALL jvmti_callback_thread_end_hook(jvmtiEnv *jvmti_env,
    JNIEnv* jni_env, jthread thread) {
  tl_thread_end();
}

void JNICALL jvmti_callback_vm_death_hook(jvmtiEnv *jvmti_env, JNIEnv* jni_env) {
  glbuffer_sendall();
  // send buffers of shutdown thread
  tl_send_buffer();
  // send object free buffer
  fh_send_buffer();

  // TODO ! suspend all *other* marked threads (they should no be in native code)
  // and send their buffers
  // you can stop them one by one using linux pid
  //   - pid id used instead of avail_thread_id as a thread id
  // resume threads after the sending thread is finished

  //jthread thread_obj;
  //jvmtiError error = (*jvmti_env)->GetCurrentThread(jvmti_env, &thread_obj);
  //check_jvmti_error(jvmti_env, error, "Cannot get object of current thread.");
  //GetAllThreads
  //SuspendThread
  //ResumeThread
  //GetThreadState

  // shutdown
  tl_print_counters();
  sender_disconnect(&sender_thread);

  pb_free();

  // NOTE: If we clean up, and daemon thread will use the structures,
  // it will crash. It is then better to leave it all as is.
  // dealloc buffers
  // cleanup blocking queues
  // cleanup java locks
}

// ******************* JVMTI entry method *******************

#ifdef WHOLE
#define VISIBLE __attribute__((externally_visible))
#else
#define VISIBLE
#endif

JNIEXPORT jint JNICALL VISIBLE
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {

#ifdef DEBUG
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

  // Local initialization.
  tls_init();

  // First of all, get hold of a JVMTI interface version 1.0.
  // Failing to obtain the interface is a fatal error.
  jvmtiEnv *jvmti_env = NULL;
  jint res = (*jvm)->GetEnv(jvm, (void **) &jvmti_env, JVMTI_VERSION_1_0);
  if (res != JNI_OK || jvmti_env == NULL) {
    fprintf(stderr, "%sUnable to access JVMTI Version 1 (0x%x),"
        " is your J2SE a 1.5 or newer version?"
        " JNIEnv's GetEnv() returned %d\n", "DiSL-RE agent error: ",
        JVMTI_VERSION_1, res);

    exit(-1);
  }

  // Request JVMTI capabilities:
  //
  //  - all class events
  //  - object tagging
  //  - object free notification
  //
  jvmtiCapabilities cap;
  memset(&cap, 0, sizeof(cap));
  cap.can_generate_all_class_hook_events = 1;
  cap.can_tag_objects = 1;
  cap.can_generate_object_free_events = 1;

  jvmtiError error;
  error = (*jvmti_env)->AddCapabilities(jvmti_env, &cap);
  check_jvmti_error(jvmti_env, error,
      "Unable to get necessary JVMTI capabilities.");

  // adding callbacks
  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));

  callbacks.ClassFileLoadHook = &jvmti_callback_class_file_load_hook;
  callbacks.ClassPrepare = &jvmti_callback_class_prepare_hook;
  callbacks.ObjectFree = &jvmti_callback_object_free_hook;
  callbacks.VMStart = &jvmti_callback_vm_start_hook;
  callbacks.VMDeath = &jvmti_callback_vm_death_hook;
  callbacks.ThreadEnd = &jvmti_callback_thread_end_hook;

  error = (*jvmti_env)->SetEventCallbacks(jvmti_env, &callbacks,
      (jint) sizeof(callbacks));
  check_jvmti_error(jvmti_env, error, "Cannot set callbacks");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set class load hook");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_CLASS_PREPARE, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set class prepare hook");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_OBJECT_FREE, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set object free hook");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_VM_START, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set jvm start hook");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_VM_DEATH, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set jvm death hook");

  error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
      JVMTI_EVENT_THREAD_END, NULL);
  check_jvmti_error(jvmti_env, error, "Cannot set thread end hook");

  // init blocking queues
  pb_init();

  glbuffer_init(jvmti_env);
  fh_init(jvmti_env);
  ot_init(jvmti_env);

  sender_init(options);

  sender_connect(&sender_thread);

  return 0;
}
