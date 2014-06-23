#ifndef _PROCESSBUFFS_H
#define	_PROCESSBUFFS_H

#include <stdlib.h>
#include <string.h>

#include <jvmti.h>

#include "buffer.h"

// queues contain process_buffs structure

// Utility queue (buffer) is specifically reserved for sending different
// messages then analysis messages. The rationale behind utility buffers is that
// at least one utility buffer is available or will be available in the near
// future no matter what. One of the two must hold:
// 1) Acquired buffer is send without any additional locking in between.
// 2) Particular "usage" (place of use) may request only constant number of
//    buffers. Place and constant is described right below and the queue size is
//    sum of all constants here

//    buffer for case 1)                     1
//    object free message                    1
//    new class info message                 1
//    just to be sure (parallelism for 1)    3
#define BQ_UTILITY 6

// number of all buffers - used for analysis with some exceptions
#define BQ_BUFFERS 32

// owner_id can have several states
// > 0 && <= TO_BUFFER_MAX_ID
//    - means that buffer is reserved for total ordering events
// >= STARTING_THREAD_ID
//    - means that buffer is owned by some thread that is marked
// == -1 - means that buffer is owned by some thread that is NOT tagged

// == PB_FREE - means that buffer is currently free
#define PB_FREE  -100

// == PB_OBJTAG - means that buffer is scheduled (processed) for object tagging
#define PB_OBJTAG  -101

// == PB_SEND - means that buffer is scheduled (processed) for sending
#define PB_SEND  -102

// == PB_UTILITY - means that this is special utility buffer
#define PB_UTILITY -1000

typedef struct {
  buffer * command_buff;
  buffer * analysis_buff;
  jlong owner_id;
} process_buffs;

void pb_init();

process_buffs * pb_get(jlong thread_id);
void pb_release(process_buffs * buffs);

process_buffs * pb_utility_get();
void pb_utility_release(process_buffs * buffs);

#endif	/* _PROCESSBUFFS_H */
