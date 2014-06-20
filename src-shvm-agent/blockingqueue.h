#ifndef _BLOCKINGQUEUE_H
#define	_BLOCKINGQUEUE_H

#include <string.h>

#include <pthread.h>

#include <jvmti.h>
#include <jni.h>

#include "jvmtiutil.h"

typedef struct {

	// array of elements
	char * qarray;
	size_t qa_size;
	size_t qa_element_size;

	size_t first;
	size_t occupied;

	pthread_mutex_t mutex;
	pthread_cond_t cond;

	jvmtiEnv * jvmti;

} blocking_queue;

// ** Monitor helper functions **

void _bq_monitor_enter(blocking_queue * bq);

void _bq_monitor_exit(blocking_queue * bq);

void _bq_monitor_wait(blocking_queue * bq);

void _bq_monitor_notify_all(blocking_queue * bq);

// ** Blocking queue functions **

void bq_create(jvmtiEnv *jvmti, blocking_queue * bq, size_t queue_capacity,
		size_t queue_element_size);

void bq_term (blocking_queue * bq);

void bq_push(blocking_queue * bq, void * data);

void bq_pop(blocking_queue * bq, void * empty);

size_t bq_length(blocking_queue * bq);

#endif	/* _BLOCKINGQUEUE_H */
