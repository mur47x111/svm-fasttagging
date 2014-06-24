#ifndef _GLOBALBUFFER_H_
#define _GLOBALBUFFER_H_

#include <jvmti.h>

#include "shared/buffer.h"

// *** buffers for total ordering ***

typedef struct {
  process_buffs * pb;
  jint analysis_count;
  size_t analysis_count_pos;
} to_buff_struct;

void glbuffer_init(jvmtiEnv *env);

void glbuffer_copy_from_tlbuffer();

void glbuffer_sendall();

#endif /* _GLOBALBUFFER_H_ */
