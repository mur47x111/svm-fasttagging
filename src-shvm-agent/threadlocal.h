#ifndef _THREADLOCAL_H
#define	_THREADLOCAL_H

#include "../src-disl-agent/jvmtiutil.h"
#include "processbuffs.h"

// *** Thread locals ***

// NOTE: The JVMTI functionality allows to implement everything
// using JVM, but the GNU implementation is faster and WORKING


#define INVALID_BUFF_ID -1
#define INVALID_THREAD_ID -1

struct tldata {
  jlong id;
  process_buffs * local_pb;
  jbyte to_buff_id;
  process_buffs * pb;
  buffer * analysis_buff;
  buffer * command_buff;
  jint analysis_count;
  size_t analysis_count_pos;
  size_t args_length_pos;
};


#if defined (__APPLE__) && defined (__MACH__)

//
// Use pthreads on Mac OS X
//

pthread_key_t tls_key;


void tls_init () {
  int result = pthread_key_create (& tls_key, NULL);
  check_error(result != 0, "Failed to allocate thread-local storage key");
}


inline struct tldata * tld_init (struct tldata * tld) {
  tld->id= INVALID_THREAD_ID;
  tld->local_pb = NULL;
  tld->to_buff_id = INVALID_BUFF_ID;
  tld->pb = NULL;
  tld->analysis_buff = NULL;
  tld->analysis_count = 0;
  tld->analysis_count_pos = 0;

  return tld;
}

struct tldata * tld_create ()  {
  struct tldata * tld = malloc (sizeof (struct tldata));
  check_error (tld == NULL, "Failed to allocate thread-local data");
  int result = pthread_setspecific (tls_key, tld);
  check_error (result != 0, "Failed to store thread-local data");
  return tld_init (tld);
}

inline struct tldata * tld_get () {
  struct tldata * tld = pthread_getspecific (tls_key);
  return (tld != NULL) ? tld : tld_create ();
}

#else

//
// Use GNU __thread where supported
//

static void tls_init () {
  // empty
}


static __thread struct tldata tld = {
    .id = INVALID_THREAD_ID,
    .local_pb = NULL,
    .to_buff_id = INVALID_BUFF_ID,
    .pb = NULL,
    .analysis_buff = NULL,
    .command_buff = NULL,
    .analysis_count = 0,
    .analysis_count_pos = 0,
};

inline static struct tldata * tld_get () {
  return & tld;
}

#endif

#endif	/* _THREADLOCAL_H */
