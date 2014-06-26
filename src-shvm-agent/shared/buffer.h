#ifndef _BUFFER_H
#define	_BUFFER_H

#include <stdlib.h>

#include <jvmti.h>

typedef struct {
	unsigned char * buff;
	size_t occupied;
	size_t capacity;
} buffer;

typedef struct {
  buffer * analysis_buff;
  jlong owner_id;
} process_buffs;

void buffer_alloc(buffer * b);

void buffer_free(buffer * b);

void buffer_fill(buffer * b, const void * data, size_t data_length);

// the space has to be already filled with data - no extensions
void buffer_fill_at_pos(buffer * b, size_t pos, const void * data,
		size_t data_length);

void buffer_read(buffer * b, size_t pos, void * data, size_t data_length);

size_t buffer_filled(buffer * b);

void buffer_clean(buffer * b);

#endif	/* _BUFFER_H */
