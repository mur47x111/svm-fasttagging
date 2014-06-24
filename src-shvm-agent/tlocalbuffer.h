#ifndef _TLOCALBUFFER_H_
#define _TLOCALBUFFER_H_

#include <jvmti.h>

#include "shared/buffer.h"

void tl_init(jvmtiEnv * env);

void tl_insert_analysis_item(jshort analysis_method_id);
void tl_insert_analysis_item_ordering(jshort analysis_method_id,
    jbyte ordering_id);
void tl_analysis_end();

void tl_send_buffer();
void tl_thread_end();

#endif /* _TLOCALBUFFER_H_ */
