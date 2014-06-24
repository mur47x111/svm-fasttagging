#ifndef _GLOBALBUFFER_H_
#define _GLOBALBUFFER_H_

#include <jvmti.h>

#include "shared/buffer.h"

// *** buffers for total ordering ***

#define TO_BUFFER_MAX_ID 127                    // byte is the holding type
#define TO_BUFFER_COUNT (TO_BUFFER_MAX_ID + 1)  // +1 for buffer id 0

// number of analysis requests in one message
#define ANALYSIS_COUNT 16384

typedef struct {
  process_buffs * pb;
  jint analysis_count;
  size_t analysis_count_pos;
} to_buff_struct;

void glbuffer_init(jvmtiEnv *env);

void glbuffer_commit();

void glbuffer_sendall();

#endif /* _GLOBALBUFFER_H_ */
