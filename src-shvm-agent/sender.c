#include <stdlib.h>
#include <pthread.h>

#include <netdb.h>
#include <unistd.h>

#include "sender.h"

#include "shared/blockingqueue.h"
#include "shared/messagetype.h"
#include "shared/threadlocal.h"

#include "pbmanager.h"

#include "../src-disl-agent/jvmtiutil.h"

// ******************* Communication *******************

// defaults - be sure that space in host_name is long enough
static const char * DEFAULT_HOST = "localhost";
static const char * DEFAULT_PORT = "11218";

// port and name of the instrumentation server
static char host_name[1024];
static char port_number[6]; // including final 0

static void parse_agent_options(char *options) {
  // assign defaults
  strcpy(host_name, DEFAULT_HOST);
  strcpy(port_number, DEFAULT_PORT);

  // no options found
  if (options == NULL) {
    return;
  }

  char * port_start = strchr(options, ':');

  // process port number
  if (port_start != NULL) {
    // replace PORT_DELIM with end of the string (0)
    port_start[0] = '\0';

    // move one char forward to locate port number
    ++port_start;

    // convert number
    int fitsP = strlen(port_start) < sizeof(port_number);
    check_error(!fitsP, "Port number is too long");

    strcpy(port_number, port_start);
  }

  // check if host_name is big enough
  int fitsH = strlen(options) < sizeof(host_name);
  check_error(!fitsH, "Host name is too long");

  strcpy(host_name, options);
}

static void send_data(int sockfd, buffer * b) {
  // send data
  // NOTE: normally access the buffer using methods
  size_t sent = 0;

  while (sent != b->occupied) {
    int res = send(sockfd, ((unsigned char *) b->buff) + sent,
        (b->occupied - sent), 0);
    check_std_error(res == -1, "Error while sending data to server");
    sent += res;
  }
}

static int open_connection() {
  // get host address
  struct addrinfo * addr;
  int gai_res = getaddrinfo(host_name, port_number, NULL, &addr);
  check_error(gai_res != 0, gai_strerror(gai_res));

  // create stream socket
  int sockfd = socket(addr->ai_family, SOCK_STREAM, 0);
  check_std_error(sockfd == -1, "Cannot create socket");

  // connect to server
  int conn_res = connect(sockfd, addr->ai_addr, addr->ai_addrlen);
  check_std_error(conn_res == -1, "Cannot connect to server");

  // free host address info
  freeaddrinfo(addr);
  return sockfd;
}

static void close_connection(int sockfd) {
  process_buffs * pb = pb_normal_get(tld_get()->id);

  messager_close_header(pb->analysis_buff);
  send_data(sockfd, pb->analysis_buff);

  pb_normal_release(pb);
  close(sockfd);
}

// ******************* Sender routines *******************

typedef enum {
  NEW_CLASS, CLASS_INFO, STRING_INFO, THREAD_INFO, META_TYPE_SIZE
} META_DATA;

static volatile int no_sending_work = 0;

static blocking_queue send_q;

static buffer *meta_buff[META_TYPE_SIZE];
static pthread_mutex_t sender_lock[META_TYPE_SIZE];

static void *sender_loop(void * obj) {
  int sockfd = open_connection();

  buffer * meta_buff_swap[META_TYPE_SIZE];

  for (int i = 0; i < META_TYPE_SIZE; i++) {
    meta_buff_swap[i] = malloc(sizeof(buffer));
    buffer_alloc(meta_buff_swap[i]);
  }

  // exit when the jvm is terminated and there are no msg to process
  while (!(no_sending_work && bq_length(&send_q) == 0)) {
    // get buffer
    // TODO thread could timeout here with timeout about 5 sec and check
    // if all of the buffers are allocated by the application threads
    // and all application threads are waiting on free buffer - deadlock
    process_buffs * pb;
    bq_pop(&send_q, &pb);

    // bug fixed: the sender thread might be blocked at the line above
    //
    // meta buffer contains NewClass event and ClassInfo event,
    // will be sent first

    // swap meta buffers in reversed ordering
    for (int i = META_TYPE_SIZE - 1; i >= 0; i--) {
      if (meta_buff[i]->occupied > 0) {
        pthread_mutex_lock(&sender_lock[i]);
        {
          buffer *temp = meta_buff_swap[i];
          meta_buff_swap[i] = meta_buff[i];
          meta_buff[i] = temp;
        }
        pthread_mutex_unlock(&sender_lock[i]);
      }
    }

    // send meta buffers in normal ordering
    for (int i = 0; i < META_TYPE_SIZE; i++) {
      if (meta_buff_swap[i]->occupied > 0) {
        // send meta buffer
        send_data(sockfd, meta_buff_swap[i]);
        buffer_clean(meta_buff_swap[i]);
      }
    }

    // send analysis buffer
    send_data(sockfd, pb->analysis_buff);

    // release (enqueue) buffer according to the type
    if (pb->owner_id == PB_UTILITY) {
      // utility buffer
      pb_utility_release(pb);
    } else {
      // normal buffer
      pb_normal_release(pb);
    }
  }

  close_connection(sockfd);
  return NULL;
}

void sender_flush() {
  // send empty buff to sending thread -> ensures exit if waiting
  process_buffs *buffs = pb_normal_get(tld_get()->id);
  sender_enqueue(buffs);
}

void sender_init(char *options) {
  parse_agent_options(options);

  bq_create(&send_q, BQ_BUFFERS + BQ_UTILITY, sizeof(process_buffs *));

  for (int i = 0; i < META_TYPE_SIZE; i++) {
    // create buffer for each type of message
    meta_buff[i] = malloc(sizeof(buffer));
    buffer_alloc(meta_buff[i]);

    // create lock for each type of message
    int pmi = pthread_mutex_init(&sender_lock[i], NULL);
    check_std_error(pmi != 0, "Cannot create pthread mutex");
  }
}

void sender_connect(pthread_t *sender_thread) {
  int res = pthread_create(sender_thread, NULL, sender_loop, NULL);
  check_error(res != 0, "Cannot create sending thread");
}

void sender_disconnect(pthread_t *sender_thread) {
  no_sending_work = 1;

  // TODO if multiple sending threads, multiple empty buffers have to be send
  // TODO also the buffers should be numbered according to the arrival to the
  // sending queue - has to be supported by the queue itself

  // send empty buff to sending thread -> ensures exit if waiting
  sender_flush();

  // wait for thread end
  int res = pthread_join(*sender_thread, NULL);
  check_error(res != 0, "Cannot join sending thread.");
}

void sender_enqueue(process_buffs * pb) {
  if (pb->owner_id != PB_UTILITY) {
    pb->owner_id = PB_SEND;
  }

  bq_push(&send_q, &pb);
}

void sender_newclass(const char* name, jsize name_len, jlong loader_id,
    jint class_data_len, const unsigned char* class_data) {
  pthread_mutex_lock(&sender_lock[NEW_CLASS]);
  {
    messager_newclass_header(meta_buff[NEW_CLASS], name, name_len, loader_id,
        class_data_len, class_data);
  }
  pthread_mutex_unlock(&sender_lock[NEW_CLASS]);
}

void sender_classinfo(jlong tag, const char* class_sig, jsize class_sig_len,
    const char* class_gen, jsize class_gen_len, jlong class_loader_tag,
    jlong super_class_tag) {
  pthread_mutex_lock(&sender_lock[NEW_CLASS]);
  {
    messager_classinfo_header(meta_buff[NEW_CLASS], tag, class_sig,
        class_sig_len, class_gen, class_gen_len, class_loader_tag,
        super_class_tag);
  }
  pthread_mutex_unlock(&sender_lock[NEW_CLASS]);
}

void sender_stringinfo(jlong str_tag, const char * str, jsize str_len) {
  pthread_mutex_lock(&sender_lock[STRING_INFO]);
  {
    messager_stringinfo_header(meta_buff[STRING_INFO], str_tag, str, str_len);
  }
  pthread_mutex_unlock(&sender_lock[STRING_INFO]);
}

void sender_threadinfo(jlong thread_tag, const char *str, jsize str_len,
    jboolean is_daemon) {
  pthread_mutex_lock(&sender_lock[THREAD_INFO]);
  {
    messager_threadinfo_header(meta_buff[THREAD_INFO], thread_tag, str, str_len,
        is_daemon);
  }
  pthread_mutex_unlock(&sender_lock[THREAD_INFO]);
}
