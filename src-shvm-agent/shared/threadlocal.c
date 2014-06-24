#include <pthread.h>

#include "threadlocal.h"

#include "../../src-disl-agent/jvmtiutil.h"

// *** Thread locals ***

#if defined (__APPLE__) && defined (__MACH__)

//
// Use pthreads on Mac OS X
//

pthread_key_t tls_key;

void tls_init() {
  int result = pthread_key_create(&tls_key, NULL);
  check_error(result != 0, "Failed to allocate thread-local storage key");
}

static tldata * tld_init(tldata * tld) {
  tld->id = INVALID_THREAD_ID;
  tld->local_pb = NULL;
  tld->to_buff_id = INVALID_BUFF_ID;
  tld->pb = NULL;
  tld->analysis_buff = NULL;
  tld->analysis_count = 0;
  tld->analysis_count_pos = 0;

  return tld;
}

static tldata * tld_create() {
  tldata * tld = malloc(sizeof(tldata));
  check_error(tld == NULL, "Failed to allocate thread-local data");
  int result = pthread_setspecific(tls_key, tld);
  check_error(result != 0, "Failed to store thread-local data");
  return tld_init(tld);
}

tldata * tld_get() {
  tldata * tld = pthread_getspecific(tls_key);
  return (tld != NULL) ? tld : tld_create();
}

#else

//
// Use GNU __thread where supported
//

void tls_init () {
  // empty
}

static __thread tldata tld = {
  .id = INVALID_THREAD_ID,
  .local_pb = NULL,
  .to_buff_id = INVALID_BUFF_ID,
  .pb = NULL,
  .analysis_buff = NULL,
  .command_buff = NULL,
  .analysis_count = 0,
  .analysis_count_pos = 0,
};

tldata * tld_get () {
  return & tld;
}

#endif
