#include <string.h>

#include "messagetype.h"

#include "buffpack.h"

#define MSG_CLOSE         0   // closing connection
#define MSG_ANALYZE       1   // sending analysis
#define MSG_OBJ_FREE      2   // sending object free
#define MSG_NEW_CLASS     3   // sending new class
#define MSG_CLASS_INFO    4   // sending class info
#define MSG_STRING_INFO   5   // sending string info
#define MSG_REG_ANALYSIS  6   // sending registration for analysis method
#define MSG_THREAD_INFO   7   // sending thread info
#define MSG_THREAD_END    8   // sending thread end message

void messager_close_header(buffer *buff) {
  pack_byte(buff, MSG_CLOSE);
}

size_t messager_analyze_header(buffer *buff, jlong ordering_id) {
  pack_byte(buff, MSG_ANALYZE);
  pack_long(buff, ordering_id);

  // get pointer to the location where count of requests will stored
  size_t pos = buffer_filled(buff);
  // request count space initialization
  pack_int(buff, 0xBAADF00D);
  return pos;
}

size_t messager_analyze_item(buffer *buff, jshort analysis_id) {
  pack_short(buff, analysis_id);

  // position of the short indicating the length of marshalled arguments
  size_t pos = buffer_filled(buff);
  // initial value of the length of the marshalled arguments
  pack_short(buff, 0xBAAD);
  return pos;
}

size_t messager_objfree_header(buffer *buff) {
  pack_byte(buff, MSG_OBJ_FREE);
  // get pointer to the location where count of requests will stored
  size_t pos = buffer_filled(buff);
  // request count space initialization
  pack_int(buff, 0xBAADF00D);
  return pos;
}

void messager_objfree_item(buffer *buff, jlong tag) {
  pack_long(buff, tag);
}

void messager_newclass_header(buffer *buff, const char* name, jlong loader_tag,
    jint class_data_len, const unsigned char* class_data) {
  pack_byte(buff, MSG_NEW_CLASS);
  // class name
  pack_string_utf8(buff, name, strlen(name));
  // class loader id
  pack_long(buff, loader_tag);
  // class code length
  pack_int(buff, class_data_len);
  // class code
  pack_bytes(buff, class_data, class_data_len);
}

void messager_classinfo_header(buffer *buff, jlong class_tag,
    const char *class_sig, const char *class_gen, jlong loader_tag,
    jlong super_class_tag) {
  pack_byte(buff, MSG_CLASS_INFO);
  // class id
  pack_long(buff, class_tag);
  // class signature
  pack_string_utf8(buff, class_sig, strlen(class_sig));
  // class generic string
  pack_string_utf8(buff, class_gen, strlen(class_gen));
  // class loader id
  pack_long(buff, loader_tag);
  // super class id
  pack_long(buff, super_class_tag);
}

void messager_stringinfo_header(buffer *buff, jlong str_tag, const char * str,
    jsize str_len) {
  pack_byte(buff, MSG_STRING_INFO);
  // send string net reference
  pack_long(buff, str_tag);
  // send string
  pack_string_utf8(buff, str, str_len);
}

void messager_reganalysis_header(buffer *buff, jshort analysis_id,
    const char * str, jsize str_len) {
  pack_byte(buff, MSG_REG_ANALYSIS);
  // new id for analysis method
  pack_short(buff, analysis_id);
  // method descriptor
  pack_string_utf8(buff, str, str_len);
}

void messager_threadinfo_header(buffer *buff, jlong thread_tag, const char *str,
    jsize str_len, jboolean is_daemon) {
  pack_byte(buff, MSG_THREAD_INFO);
  pack_long(buff, thread_tag);
  pack_string_utf8(buff, str, str_len);
  pack_boolean(buff, is_daemon);
}

void messager_threadend_header(buffer *buff, jlong thread_id) {
  pack_byte(buff, MSG_THREAD_END);
  pack_long(buff, thread_id);
}
