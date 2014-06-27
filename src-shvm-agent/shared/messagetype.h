#ifndef _MESSAGETYPE_H
#define	_MESSAGETYPE_H

#include <jvmti.h>

#include "buffer.h"

void messager_close_header(buffer *buff);

size_t messager_analyze_header(buffer *buff, jlong ordering_id);
size_t messager_analyze_item(buffer *buff, jshort analysis_id);

size_t messager_objfree_header(buffer *buff);
void messager_objfree_item(buffer *buff, jlong tag);

void messager_newclass_header(buffer *buff, const char* name, jsize name_len,
		jlong loader_tag, jint class_data_len, const unsigned char* class_data);

void messager_classinfo_header(buffer *buff, jlong class_tag,
		const char *class_sig, jsize class_sig_len, const char *class_gen,
		jsize class_gen_len, jlong loader_tag, jlong super_class_tag);

void messager_stringinfo_header(buffer *buff, jlong str_tag, const char * str,
    jsize str_len);

void messager_reganalysis_header(buffer *buff, jshort analysis_id,
    const char * str, jsize str_len);

void messager_threadinfo_header(buffer *buff, jlong thread_tag, const char *str,
    jsize str_len, jboolean is_daemon);

void messager_threadend_header(buffer *buff, jlong thread_id);

#endif	/* _MESSAGETYPE_H */
