#ifndef _BLOCKINGQUEUE_H
#define	_BLOCKINGQUEUE_H

#include <pthread.h>

typedef struct {
  // array of elements
  char * qarray;
  size_t qa_size;
  size_t qa_element_size;

  size_t first;
  size_t occupied;

  pthread_mutex_t mutex;
  pthread_cond_t cond;

} blocking_queue;

// ** Blocking queue functions **

void bq_create(blocking_queue * bq, size_t queue_capacity,
    size_t queue_element_size);

void bq_term(blocking_queue * bq);

void bq_push(blocking_queue * bq, void * data);

void bq_pop(blocking_queue * bq, void * empty);

size_t bq_length(blocking_queue * bq);

#endif	/* _BLOCKINGQUEUE_H */
