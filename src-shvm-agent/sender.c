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

  messager_close_header(pb->command_buff);
  send_data(sockfd, pb->command_buff);

  pb_normal_release(pb);
  close(sockfd);
}

// ******************* Sender routines *******************

static blocking_queue send_q;

static volatile int no_sending_work = 0;

static buffer * meta_buff;

pthread_mutex_t sender_lock;

static void *sender_loop(void * obj) {
  int sockfd = open_connection();

  buffer * meta_buff_swap = malloc(sizeof(buffer));
  buffer_alloc (meta_buff_swap);

  // exit when the jvm is terminated and there are no msg to process
  while (!(no_sending_work && bq_length(&send_q) == 0)) {
    // meta buffer contains NewClass event and ClassInfo event,
    // will be sent first
    if (meta_buff->occupied > 0) {
      //swap meta buffer
      pthread_mutex_lock(&sender_lock);
      {
        buffer *temp = meta_buff_swap;
        meta_buff_swap = meta_buff;
        meta_buff = temp;
      }
      pthread_mutex_unlock(&sender_lock);

      // send meta buffer
      send_data(sockfd, meta_buff_swap);
      buffer_clean(meta_buff_swap);
    }

    // get buffer
    // TODO thread could timeout here with timeout about 5 sec and check
    // if all of the buffers are allocated by the application threads
    // and all application threads are waiting on free buffer - deadlock
    process_buffs * pb;
    bq_pop(&send_q, &pb);

    // first send command buffer - contains new class or object ids,...
    send_data(sockfd, pb->command_buff);
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

void sender_init(char *options) {
  parse_agent_options(options);

  int pmi = pthread_mutex_init(&sender_lock, NULL);
  check_std_error(pmi != 0, "Cannot create pthread mutex");

  bq_create(&send_q, BQ_BUFFERS + BQ_UTILITY, sizeof(process_buffs *));

  meta_buff = malloc(sizeof(buffer));
  buffer_alloc(meta_buff);
}

void sender_connect(pthread_t *sender_thread, int size) {
  for (int i = 0; i < size; i++) {
    int res = pthread_create(&sender_thread[i], NULL, sender_loop, NULL);
    check_error(res != 0, "Cannot create sending thread");
  }
}

void sender_disconnect(pthread_t *sender_thread, int size) {
  no_sending_work = 1;

  // TODO if multiple sending threads, multiple empty buffers have to be send
  // TODO also the buffers should be numbered according to the arrival to the
  // sending queue - has to be supported by the queue itself

  // send empty buff to sending thread -> ensures exit if waiting
  for (int i = 0; i < size; i++) {
    process_buffs *buffs = pb_normal_get(tld_get()->id);
    sender_enqueue(buffs);
  }

  // wait for thread end
  for (int i = 0; i < size; i++) {
    int res = pthread_join(sender_thread[i], NULL);
    check_error(res != 0, "Cannot join sending thread.");
  }
}

void sender_enqueue(process_buffs * pb) {
  if (pb->owner_id != PB_UTILITY) {
    pb->owner_id = PB_SEND;
  }

  bq_push(&send_q, &pb);
}

void sender_newclass(const char* name, jlong loader_id, jint class_data_len,
    const unsigned char* class_data) {
  pthread_mutex_lock(&sender_lock);
  {
    messager_newclass_header(meta_buff, name, loader_id, class_data_len,
        class_data);
  }
  pthread_mutex_unlock(&sender_lock);
}

void sender_classinfo(jlong tag, const char* class_sig, const char* class_gen,
    jlong class_loader_tag, jlong super_class_tag) {
  pthread_mutex_lock(&sender_lock);
  {
    messager_classinfo_header(meta_buff, tag, class_sig, class_gen,
        class_loader_tag, super_class_tag);
  }
  pthread_mutex_unlock(&sender_lock);
}
