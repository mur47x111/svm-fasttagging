#ifndef _PROCESSBUFFS_H
#define	_PROCESSBUFFS_H

#include <stdlib.h>
#include <string.h>

#include "buffer.h"

#include "jvmtiutil.h"
#include "netref.h"

#include "blockingqueue.h"

// *** Sync queues ***

// queue with empty buffers
blocking_queue empty_q;

// queue where buffers are queued for object
blocking_queue objtag_q;

// queue with empty utility buffers
blocking_queue utility_q;

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

typedef struct {
  buffer * command_buff;
  buffer * analysis_buff;
  jlong owner_id;
} process_buffs;

// list of all allocated bq buffers
process_buffs pb_list[BQ_BUFFERS + BQ_UTILITY];

process_buffs * buffs_get(jlong thread_id);



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

process_buffs * buffs_utility_get();

process_buffs * buffs_utility_send(process_buffs * buffs);

// normally only sending thread should access this function
void _buffs_utility_release(process_buffs * buffs);

// normally only sending thread should access this function
void _buffs_release(process_buffs * buffs);

void buffs_objtag(process_buffs * buffs);

// only objtag thread should access this function
process_buffs * _buffs_objtag_get();

void _buffs_send(process_buffs * buffs);

// only sending thread should access this function
process_buffs * _buffs_send_get();

#endif	/* _PROCESSBUFFS_H */
