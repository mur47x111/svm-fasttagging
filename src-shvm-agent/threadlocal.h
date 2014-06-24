#ifndef _THREADLOCAL_H
#define	_THREADLOCAL_H

#include "../src-disl-agent/jvmtiutil.h"
#include "processbuffs.h"

// *** Thread locals ***

// NOTE: The JVMTI functionality allows to implement everything
// using JVM, but the GNU implementation is faster and WORKING

#define INVALID_BUFF_ID -1
#define INVALID_THREAD_ID -1

#define TO_BUFFER_MAX_ID 127 // byte is the holding type
#define STARTING_THREAD_ID (TO_BUFFER_MAX_ID + 1)

typedef struct {
  jlong id;
  process_buffs * local_pb;
  jbyte to_buff_id;
  process_buffs * pb;
  buffer * analysis_buff;
  buffer * command_buff;
  jint analysis_count;
  size_t analysis_count_pos;
  size_t args_length_pos;
} tldata;

void tls_init();
tldata * tld_get();

#endif	/* _THREADLOCAL_H */
