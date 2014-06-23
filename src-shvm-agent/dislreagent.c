#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include <pthread.h>

#include <jvmti.h>
#include <jni.h>

#include "jvmtiutil.h"

#include "messagetype.h"
#include "buffer.h"
#include "buffpack.h"
#include "blockingqueue.h"
#include "netref.h"

#include "dislreagent.h"

#include "processbuffs.h"
#include "threadlocal.h"

#include "sender.h"

#include "tagger.h"

static jvmtiEnv * jvmti_env;
static JavaVM * java_vm;

// number of analysis requests in one message
#define ANALYSIS_COUNT 16384

// *** buffers for total ordering ***

typedef struct {
	process_buffs * pb;
	jint analysis_count;
	size_t analysis_count_pos;
} to_buff_struct;

#define TO_BUFFER_MAX_ID 127 // byte is the holding type
#define TO_BUFFER_COUNT (TO_BUFFER_MAX_ID + 1) // +1 for buffer id 0

static jrawMonitorID to_buff_lock;

static to_buff_struct to_buff_array[TO_BUFFER_COUNT];

// *** buffer for object free ***

#define MAX_OBJ_FREE_EVENTS 4096

static process_buffs * obj_free_buff = NULL;
static jint obj_free_event_count = 0;
static size_t obj_free_event_count_pos = 0;

static jrawMonitorID obj_free_lock;

// first available id for new messages
static volatile jshort avail_analysis_id = 1;

#define STARTING_THREAD_ID (TO_BUFFER_MAX_ID + 1)

// initial ids are reserved for total ordering buffers
static volatile jlong avail_thread_id = STARTING_THREAD_ID;

// ******************* Advanced packing routines *******************

static void _fill_ot_rec(JNIEnv * jni_env, buffer * cmd_buff,
		unsigned char ot_type, buffer * buff, jstring to_send) {

	// crate object tagging record
	objtag_rec ot_rec;
	// type of object to be tagged
	ot_rec.obj_type = ot_type;
	// position in the buffer, where the data will be stored during tagging
	ot_rec.buff_pos = buffer_filled(buff);
	// global reference to the object to be tagged
	ot_rec.obj_to_tag = (*jni_env)->NewGlobalRef(jni_env, to_send);

	// save to command buff
	buffer_fill(cmd_buff, &ot_rec, sizeof(ot_rec));
}

static void pack_object(JNIEnv * jni_env, buffer * buff, buffer * cmd_buff,
		jobject to_send, unsigned char object_type) {

	// create entry for object tagging thread that will replace the null ref
	if(to_send != NULL) {
		_fill_ot_rec(jni_env, cmd_buff, object_type, buff, to_send);
	}

	// pack null net reference
	pack_long(buff, NULL_NET_REF);
}

static void buff_put_short(buffer * buff, size_t buff_pos, jshort to_put) {
	// put the short at the position in network order
	jshort nts = htons(to_put);
	buffer_fill_at_pos(buff, buff_pos, &nts, sizeof(jshort));
}


static void buff_put_int(buffer * buff, size_t buff_pos, jint to_put) {
	// put the int at the position in network order
	jint nts = htonl(to_put);
	buffer_fill_at_pos(buff, buff_pos, &nts, sizeof(jint));
}


// ******************* analysis helper methods *******************

static jshort next_analysis_id () {
	// get id for this method string
	// this could use different lock then tagging but it should not be a problem
	// and it will be used rarely - bit unoptimized

	jshort result = -1;
	enter_critical_section(jvmti_env, obj_free_lock);
	{
		result = avail_analysis_id++;
	}
	exit_critical_section(jvmti_env, obj_free_lock);

	return result;
}

static jshort register_method(
		JNIEnv * jni_env, jstring analysis_method_desc,
		jlong thread_id) {
#ifdef DEBUG
	printf("Registering method (thread %ld)\n", tld_get()->id);
#endif

	// *** send register analysis method message ***

	// request unique id
	jshort new_analysis_id = next_analysis_id();

	// get string length
	jsize str_len =
			(*jni_env)->GetStringUTFLength(jni_env, analysis_method_desc);

	// get string data as utf-8
	const char * str =
			(*jni_env)->GetStringUTFChars(jni_env, analysis_method_desc, NULL);
	check_error(str == NULL, "Cannot get string from java");

	// check if the size is sendable
	int size_fits = str_len < UINT16_MAX;
	check_error(! size_fits, "Java string is too big for sending");

	// obtain buffer
	process_buffs * buffs = buffs_utility_get();
	buffer * buff = buffs->analysis_buff;

	// msg id
	pack_byte(buff, MSG_REG_ANALYSIS);
	// new id for analysis method
	pack_short(buff, new_analysis_id);
	// method descriptor
	pack_string_utf8(buff, str, str_len);

	// send message
	buffs_utility_send(buffs);

	// release string
	(*jni_env)->ReleaseStringUTFChars(jni_env, analysis_method_desc, str);

#ifdef DEBUG
	printf("Method registered (thread %ld)\n", tld_get()->id);
#endif

	return new_analysis_id;
}


static jlong next_thread_id () {
#ifdef DEBUG
	printf("Marking thread (thread %ld)\n", tld_get()->id);
#endif
	// mark the thread - with lock
	// TODO replace total ordering lock with private lock - perf. issue
	jlong result = -1;
	enter_critical_section(jvmti_env, to_buff_lock);
	{
		result = avail_thread_id++;
	}
	exit_critical_section(jvmti_env, to_buff_lock);

#ifdef DEBUG
	printf("Thread marked (thread %ld)\n", result);
#endif
	return result;
}


static size_t create_analysis_request_header (
		buffer * buff, jshort analysis_method_id
) {
	// analysis method id
	pack_short(buff, analysis_method_id);

	// position of the short indicating the length of marshalled arguments
	size_t pos = buffer_filled(buff);

	// initial value of the length of the marshalled arguments
	pack_short(buff, 0xBAAD);

	return pos;
}


void analysis_start_buff(
		JNIEnv * jni_env, jshort analysis_method_id, jbyte ordering_id,
		struct tldata * tld
) {
#ifdef DEBUGANL
	printf("Analysis (buffer) start enter (thread %ld)\n", tld_get()->id);
#endif

	check_error(ordering_id < 0, "Buffer id has negative value");

	// flush normal buffers before each global buffering
	if(tld->analysis_buff != NULL) {
		// invalidate buffer pointers
		tld->analysis_buff = NULL;
		tld->command_buff = NULL;

		// send buffers for object tagging
		buffs_objtag(tld->pb);

		// invalidate buffer pointer
		tld->pb = NULL;
	}

	// allocate special local buffer for this buffering
	if(tld->local_pb == NULL) {
		// mark thread
		if(tld->id == INVALID_THREAD_ID) {
			tld->id = next_thread_id ();
		}

		// get buffers
		tld->local_pb = buffs_get(tld->id);
	}

	// set local buffers for this buffering
	tld->analysis_buff = tld->local_pb->analysis_buff;
	tld->command_buff = tld->local_pb->command_buff;

	tld->to_buff_id = ordering_id;

	// create request header, keep track of the position
	// of the length of marshalled arguments
	tld->args_length_pos = create_analysis_request_header(tld->analysis_buff, analysis_method_id);

#ifdef DEBUGANL
	printf("Analysis (buffer) start exit (thread %ld)\n", tld_get()->id);
#endif
}


static size_t create_analysis_msg(buffer * buff, jlong id) {
	// create analysis message

	// analysis msg
	pack_byte(buff, MSG_ANALYZE);

	// thread (total order buffer) id
	pack_long(buff, id);

	// get pointer to the location where count of requests will stored
	size_t pos = buffer_filled(buff);

	// request count space initialization
	pack_int(buff, 0xBAADF00D);

	return pos;
}



static void analysis_start(
		JNIEnv * jni_env, jshort analysis_method_id,
		struct tldata * tld
) {
#ifdef DEBUGANL
	printf("Analysis start enter (thread %ld)\n", tld_get()->id);
#endif

	if(tld->analysis_buff == NULL) {

		// mark thread
		if(tld->id == INVALID_THREAD_ID) {
			tld->id = next_thread_id ();
		}

		// get buffers
		tld->pb = buffs_get(tld->id);
		tld->analysis_buff = tld->pb->analysis_buff;
		tld->command_buff = tld->pb->command_buff;

		// determines, how many analysis requests are sent in one message
		tld->analysis_count = 0;

		// create analysis message
		tld->analysis_count_pos = create_analysis_msg(tld->analysis_buff, tld->id);
	}

	// create request header, keep track of the position
	// of the length of marshalled arguments
	tld->args_length_pos = create_analysis_request_header(tld->analysis_buff, analysis_method_id);

#ifdef DEBUGANL
	printf("Analysis start exit (thread %ld)\n", tld_get()->id);
#endif
}

static void correct_cmd_buff_pos(buffer * cmd_buff, size_t shift) {

	size_t cmd_buff_len = buffer_filled(cmd_buff);
	size_t read = 0;

	objtag_rec ot_rec;

	// go through all records and shift the buffer position
	while(read < cmd_buff_len) {

		// read ot_rec data
		buffer_read(cmd_buff, read, &ot_rec, sizeof(ot_rec));

		// shift buffer position
		ot_rec.buff_pos += shift;

		// write ot_rec data
		buffer_fill_at_pos(cmd_buff, read, &ot_rec, sizeof(ot_rec));

		// next
		read += sizeof(ot_rec);
	}
}

static void analysis_end_buff(struct tldata * tld) {
#ifdef DEBUGANL
	printf("Analysis (buffer) end enter (thread %ld)\n", tld_get()->id);
#endif

	// TODO lock for each buffer id

	// sending of half-full buffer is done in shutdown hook and obj free hook

	// write analysis to total order buffer - with lock
	enter_critical_section(jvmti_env, to_buff_lock);
	{
		// pointer to the total order buffer structure
		to_buff_struct * tobs = &(to_buff_array[tld->to_buff_id]);

		// allocate new buffer
		if(tobs->pb == NULL) {

			tobs->pb = buffs_get(tld->id);

			// set owner_id as t_buffid
			tobs->pb->owner_id = tld->to_buff_id;

			// determines, how many analysis requests are sent in one message
			tobs->analysis_count = 0;

			// create analysis message
			tobs->analysis_count_pos = create_analysis_msg(
					tobs->pb->analysis_buff, tld->to_buff_id);
		}

		// first correct positions in command buffer
		// records in command buffer are positioned according to the local
		// analysis buffer but we want the position to be valid in total ordered
		// buffer
		correct_cmd_buff_pos(tld->local_pb->command_buff,
				buffer_filled(tobs->pb->analysis_buff));

		// fill total order buffers
		buffer_fill(tobs->pb->analysis_buff,
				// NOTE: normally access the buffer using methods
				tld->local_pb->analysis_buff->buff,
				tld->local_pb->analysis_buff->occupied);

		buffer_fill(tobs->pb->command_buff,
				// NOTE: normally access the buffer using methods
				tld->local_pb->command_buff->buff,
				tld->local_pb->command_buff->occupied);

		// empty local buffers
		buffer_clean(tld->local_pb->analysis_buff);
		buffer_clean(tld->local_pb->command_buff);

		// add number of completed requests
		++(tobs->analysis_count);

		// buffer has to be updated each time because jvm could end and buffer
		// has to be up-to date
		buff_put_int(tobs->pb->analysis_buff, tobs->analysis_count_pos,
				tobs->analysis_count);

		// send only when the method count is reached
		if(tobs->analysis_count >= ANALYSIS_COUNT) {

			// send buffers for object tagging
			buffs_objtag(tobs->pb);

			// invalidate buffer pointer
			tobs->pb = NULL;
		}
	}
	exit_critical_section(jvmti_env, to_buff_lock);

	// reset analysis and command buffers for normal buffering
	// set to NULL, because we've send the buffers at the beginning of
	// global buffer buffering
	tld->analysis_buff = NULL;
	tld->command_buff = NULL;

	// invalidate buffer id
	tld->to_buff_id = INVALID_BUFF_ID;

#ifdef DEBUGANL
	printf("Analysis (buffer) end exit (thread %ld)\n", tld_get()->id);
#endif
}

static void analysis_end(struct tldata * tld) {
	// update the length of the marshalled arguments
	jshort args_length = buffer_filled(tld->analysis_buff) - tld->args_length_pos - sizeof (jshort);
	buff_put_short(tld->analysis_buff, tld->args_length_pos, args_length);

	// this method is also called for end of analysis for totally ordered API
	if(tld->to_buff_id != INVALID_BUFF_ID) {
		analysis_end_buff(tld);
		return;
	}

#ifdef DEBUGANL
	printf("Analysis end enter (thread %ld)\n", tld_get()->id);
#endif

	// sending of half-full buffer is done in thread end hook

	// increment the number of completed requests
	tld->analysis_count++;

	// buffer has to be updated each time - the thread can end any time
	buff_put_int(tld->analysis_buff, tld->analysis_count_pos, tld->analysis_count);

	// send only after the proper count is reached
	if(tld->analysis_count >= ANALYSIS_COUNT) {
		// invalidate buffer pointers
		tld->analysis_buff = NULL;
		tld->command_buff = NULL;

		// send buffers for object tagging
		buffs_objtag(tld->pb);

		// invalidate buffer pointer
		tld->pb = NULL;
	}

#ifdef DEBUGANL
	printf("Analysis end exit (thread %ld)\n", tld_get()->id);
#endif
}

// ******************* REDispatch methods *******************

JNIEXPORT jshort JNICALL Java_ch_usi_dag_dislre_REDispatch_registerMethod
(JNIEnv * jni_env, jclass this_class, jstring analysis_method_desc) {

	return register_method(jni_env, analysis_method_desc, tld_get()->id);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__S
(JNIEnv * jni_env, jclass this_class, jshort analysis_method_id) {

	analysis_start(jni_env, analysis_method_id, tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB
(JNIEnv * jni_env, jclass this_class, jshort analysis_method_id,
		jbyte ordering_id) {

	analysis_start_buff(jni_env, analysis_method_id, ordering_id, tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_analysisEnd
(JNIEnv * jni_env, jclass this_class) {

	analysis_end(tld_get());
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendBoolean
(JNIEnv * jni_env, jclass this_class, jboolean to_send) {

	pack_boolean(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendByte
(JNIEnv * jni_env, jclass this_class, jbyte to_send) {

	pack_byte(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendChar
(JNIEnv * jni_env, jclass this_class, jchar to_send) {

	pack_char(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendShort
(JNIEnv * jni_env, jclass this_class, jshort to_send) {

	pack_short(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendInt
(JNIEnv * jni_env, jclass this_class, jint to_send) {

	pack_int(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendLong
(JNIEnv * jni_env, jclass this_class, jlong to_send) {

	pack_long(tld_get()->analysis_buff, to_send);
}


JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendFloat
(JNIEnv * jni_env, jclass this_class, jfloat to_send) {

	pack_float(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendDouble
(JNIEnv * jni_env, jclass this_class, jdouble to_send) {

	pack_double(tld_get()->analysis_buff, to_send);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObject
(JNIEnv * jni_env, jclass this_class, jobject to_send) {

	struct tldata * tld = tld_get ();
	pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send,
			OT_OBJECT);
}

JNIEXPORT void JNICALL Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData
(JNIEnv * jni_env, jclass this_class, jobject to_send) {

	struct tldata * tld = tld_get ();
	pack_object(jni_env, tld->analysis_buff, tld->command_buff, to_send,
			OT_DATA_OBJECT);
}


static JNINativeMethod redispatchMethods[] = {
    {"registerMethod",     "(Ljava/lang/String;)S", (void *)&Java_ch_usi_dag_dislre_REDispatch_registerMethod},
    {"analysisStart",      "(S)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisStart__S},
    {"analysisStart",      "(SB)V",                 (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisStart__SB},
    {"analysisEnd",        "()V",                   (void *)&Java_ch_usi_dag_dislre_REDispatch_analysisEnd},
    {"sendBoolean",        "(Z)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendBoolean},
    {"sendByte",           "(B)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendByte},
    {"sendChar",           "(C)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendChar},
    {"sendShort",          "(S)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendShort},
    {"sendInt",            "(I)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendInt},
    {"sendLong",           "(J)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendLong},
    {"sendFloat",          "(F)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendFloat},
    {"sendDouble",         "(D)V",                  (void *)&Java_ch_usi_dag_dislre_REDispatch_sendDouble},
    {"sendObject",         "(Ljava/lang/Object;)V", (void *)&Java_ch_usi_dag_dislre_REDispatch_sendObject},
    {"sendObjectPlusData", "(Ljava/lang/Object;)V", (void *)&Java_ch_usi_dag_dislre_REDispatch_sendObjectPlusData},
};

// ******************* CLASS LOAD callback *******************

void JNICALL jvmti_callback_class_file_load_hook(
		jvmtiEnv *jvmti_env, JNIEnv* jni_env,
		jclass class_being_redefined, jobject loader,
		const char* name, jobject protection_domain,
		jint class_data_len, const unsigned char* class_data,
		jint* new_class_data_len, unsigned char** new_class_data
) {
  tagger_newclass(jni_env, jvmti_env, loader, name, class_data_len, class_data);
}


// ******************* CLASS prepare callback *******************

// registers all native methods so they can be used during VM init phase
void JNICALL jvmti_callback_class_prepare_hook(jvmtiEnv *jvmti_env,
		JNIEnv* jni_env, jthread thread, jclass klass) {

	static long registedFlag = 0;
	jvmtiError error;

	if (registedFlag) {

		// might fail due to phase problem
		(*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_DISABLE,
				JVMTI_EVENT_CLASS_PREPARE, NULL );
		return;
	}

	char * class_sig;

	error = (*jvmti_env)->GetClassSignature(jvmti_env, klass, &class_sig,
			NULL );
	check_jvmti_error(jvmti_env, error, "Cannot get class signature");

	if (strcmp(class_sig, "Lch/usi/dag/dislre/REDispatch;") == 0) {

		(*jni_env)->RegisterNatives(jni_env, klass, redispatchMethods,
				sizeof(redispatchMethods) / sizeof(redispatchMethods[0]));
		registedFlag = 1;
	}

	// deallocate memory
	error = (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *) class_sig);
	check_jvmti_error(jvmti_env, error, "Cannot deallocate memory");
}


// ******************* OBJECT FREE callback *******************

void JNICALL jvmti_callback_object_free_hook(
		jvmtiEnv *jvmti_env, jlong tag) {

	enter_critical_section(jvmti_env, obj_free_lock);
	{
		// allocate new obj free buffer
		if(obj_free_buff == NULL) {

			// obtain buffer
			obj_free_buff = buffs_utility_get();

			// reset number of events in the buffer
			obj_free_event_count = 0;

			// put initial free object msg id
			pack_byte(obj_free_buff->analysis_buff, MSG_OBJ_FREE);

			// get pointer to the location where count of requests will stored
			obj_free_event_count_pos =
					buffer_filled(obj_free_buff->analysis_buff);

			// request count space initialization
			pack_int(obj_free_buff->analysis_buff, 0xBAADF00D);
		}

		// obtain message buffer
		buffer * buff = obj_free_buff->analysis_buff;

		// buffer obj free id

		// obj id
		pack_long(buff, tag);

		// increment the number of free events
		++obj_free_event_count;

		// update the number of free events
		buff_put_int(buff, obj_free_event_count_pos, obj_free_event_count);

		if(obj_free_event_count >= MAX_OBJ_FREE_EVENTS) {

#ifdef DEBUG
			printf("Sending object free buffer (thread %ld)\n", tld_get()->id);
#endif

			// NOTE: We can queue buffer to the sending queue. This is because
			// object tagging thread is first sending the objects and then
			// deallocating the global references. We cannot have here objects
			// that weren't send already

			// NOTE2: It is mandatory to submit to the sending queue directly
			// because gc (that is generating these events) will block the
			// tagging thread. And with not working tagging thread, we can
			// run out of buffers.
			buffs_utility_send(obj_free_buff);

			// cleanup
			obj_free_buff = NULL;
			obj_free_event_count = 0;
			obj_free_event_count_pos = 0;

#ifdef DEBUG
			printf("Object free buffer sent (thread %ld)\n", tld_get()->id);
#endif
		}

	}
	exit_critical_section(jvmti_env, obj_free_lock);
}


// ******************* START callback *******************

void JNICALL jvmti_callback_vm_start_hook(
		jvmtiEnv *jvmti_env, JNIEnv* jni_env
) {
  tagger_jvmstart();
}


// ******************* INIT callback *******************

void JNICALL jvmti_callback_vm_init_hook(
		jvmtiEnv *jvmti_env, JNIEnv* jni_env, jthread thread
) {
  tagger_connect();
}


// ******************* SHUTDOWN callback *******************

static void send_all_to_buffers() {

	// send all total ordering buffers - with lock
	enter_critical_section(jvmti_env, to_buff_lock);
	{
		int i;
		for(i = 0; i < TO_BUFFER_COUNT; ++i) {

			// send all buffers for occupied ids
			if(to_buff_array[i].pb != NULL) {

				// send buffers for object tagging
				buffs_objtag(to_buff_array[i].pb);

				// invalidate buffer pointer
				to_buff_array[i].pb = NULL;
			}
		}
	}
	exit_critical_section(jvmti_env, to_buff_lock);
}

static void send_thread_buffers(struct tldata * tld) {

	// thread is marked -> worked with buffers
	jlong thread_id = tld->id;
	if(thread_id != INVALID_THREAD_ID) {

		int i;
		for(i = 0; i < BQ_BUFFERS; ++i) {
			// if buffer is owned by tagged thread, send it
			if(pb_list[i].owner_id == thread_id) {
				buffs_objtag(&(pb_list[i]));
			}
		}
	}

	tld->analysis_buff = NULL;
	tld->command_buff = NULL;
	tld->pb = NULL;
}

static void send_obj_free_buffer() {

	// send object free buffer - with lock
	enter_critical_section(jvmti_env, obj_free_lock);
	{
		if(obj_free_buff != NULL) {

			buffs_utility_send(obj_free_buff);
			obj_free_buff = NULL;
		}
	}
	exit_critical_section(jvmti_env, obj_free_lock);
}


void JNICALL jvmti_callback_vm_death_hook(
		jvmtiEnv *jvmti_env, JNIEnv* jni_env
) {
	struct tldata * tld = tld_get();

#ifdef DEBUG
	printf("Shutting down (thread %ld)\n", tld_get()->id);
#endif

	// send all buffers for total order
	send_all_to_buffers();

	// send buffers of shutdown thread
	send_thread_buffers(tld);

	// send object free buffer
	send_obj_free_buffer();

	// TODO ! suspend all *other* marked threads (they should no be in native code)
	// and send their buffers
	// you can stop them one by one using linux pid
	//   - pid id used instead of avail_thread_id as a thread id
	// resume threads after the sending thread is finished

	//jthread thread_obj;
	//jvmtiError error = (*jvmti_env)->GetCurrentThread(jvmti_env, &thread_obj);
	//check_jvmti_error(jvmti_env, error, "Cannot get object of current thread.");
	//GetAllThreads
	//SuspendThread
	//ResumeThread
	//GetThreadState

	// shutdown - first tagging then sending thread
  tagger_disconnect();
	sender_disconnect();

	// NOTE: Buffers hold by other threads can be in inconsistent state.
	// We cannot simply send them, so we at least inform the user.

	// inform about all non-send buffers
	// all buffers should be send except some daemon thread buffers
	//  - also some class loading + thread tagging buffers can be there (with 0)
	// Report: .

	int relevant_count = 0;
	int support_count = 0;
	int marked_thread_count = 0;
	int non_marked_thread_count = 0;

	int i; // C99 needed for in cycle definition :)
	for(i = 0; i < BQ_BUFFERS; ++i) {

		// buffer held by thread that performed (is still doing) analysis
		//  - probably analysis data
		if(pb_list[i].owner_id >= STARTING_THREAD_ID) {
			relevant_count += buffer_filled(pb_list[i].analysis_buff);
			support_count += buffer_filled(pb_list[i].command_buff);
			++marked_thread_count;
#ifdef DEBUG
			printf("Lost buffer for id %ld\n", pb_list[i].owner_id);
#endif
		}

		// buffer held by thread that did NOT perform analysis
		//  - support data
		if(pb_list[i].owner_id == INVALID_THREAD_ID) {
			support_count += buffer_filled(pb_list[i].analysis_buff) +
					buffer_filled(pb_list[i].command_buff);
			++non_marked_thread_count;
		}

		check_error(pb_list[i].owner_id == PB_OBJTAG,
				"Unprocessed buffers left in object tagging queue");

		check_error(pb_list[i].owner_id == PB_SEND,
				"Unprocessed buffers left in sending queue");
	}

#ifdef DEBUG
	if(relevant_count > 0 || support_count > 0) {
		fprintf(stderr, "%s%s%d%s%d%s%s%d%s%d%s",
				"Warning: ",
				"Due to non-terminated (daemon) threads, ",
				relevant_count,
				" bytes of relevant data and ",
				support_count,
				" bytes of support data were lost ",
				"(thread count - analysis: ",
				marked_thread_count,
				", helper: ",
				non_marked_thread_count,
				").\n");
	}
#endif

	// NOTE: If we clean up, and daemon thread will use the structures,
	// it will crash. It is then better to leave it all as is.
	// dealloc buffers
	// cleanup blocking queues
	// cleanup java locks

#ifdef DEBUG
	printf("Shut down complete (thread %ld)\n", tld_get()->id);
#endif
}

// ******************* THREAD END callback *******************

void JNICALL jvmti_callback_thread_end_hook(
		jvmtiEnv *jvmti_env, JNIEnv* jni_env, jthread thread
) {
	// It should be safe to use thread locals according to jvmti documentation:
	// Thread end events are generated by a terminating thread after its initial
	// method has finished execution.

	jlong thread_id = tld_get()->id;
	if(thread_id == INVALID_THREAD_ID) {
		return;
	}

	// send all pending buffers associated with this thread
	send_thread_buffers(tld_get());

	// send thread end message

	// obtain buffer
	process_buffs * buffs = buffs_get(thread_id);
	buffer * buff = buffs->analysis_buff;

	// msg id
	pack_byte(buff, MSG_THREAD_END);
	// thread id
	pack_long(buff, thread_id);

	// send to object tagging queue - this thread could have something still
	// in the queue so we ensure proper ordering
	buffs_objtag(buffs);
}

// ******************* JVMTI entry method *******************

#ifdef WHOLE
#define VISIBLE __attribute__((externally_visible))
#else
#define VISIBLE
#endif

JNIEXPORT jint JNICALL VISIBLE
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {

#ifdef DEBUG
	setvbuf(stdout, NULL, _IONBF, 0);
#endif

	//
	// Local initialization.
	//
	java_vm = jvm;
	tls_init ();

	//
	// First of all, get hold of a JVMTI interface version 1.0.
	// Failing to obtain the interface is a fatal error.
	//
	jvmti_env = NULL;
	jint res = (*jvm)->GetEnv(jvm, (void **) &jvmti_env, JVMTI_VERSION_1_0);
	if (res != JNI_OK || jvmti_env == NULL) {
//		fprintf(stderr,
//				"%sUnable to access JVMTI Version 1 (0x%x),"
//				" is your J2SE a 1.5 or newer version?"
//				" JNIEnv's GetEnv() returned %d\n",
//				ERR_PREFIX, JVMTI_VERSION_1, res
//		);
//
//		exit(ERR_JVMTI);
	  exit(-1);
	}

	//
	// Request JVMTI capabilities:
	//
	//  - all class events
	//  - object tagging
	//  - object free notification
	//
	jvmtiCapabilities cap;
	memset(&cap, 0, sizeof(cap));
	cap.can_generate_all_class_hook_events = 1;
	cap.can_tag_objects = 1;
	cap.can_generate_object_free_events = 1;

	jvmtiError error;
	error = (*jvmti_env)->AddCapabilities(jvmti_env, &cap);
	check_jvmti_error(jvmti_env, error,
			"Unable to get necessary JVMTI capabilities.");

	// adding callbacks
	jvmtiEventCallbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));

	callbacks.ClassFileLoadHook = &jvmti_callback_class_file_load_hook;
	callbacks.ClassPrepare = &jvmti_callback_class_prepare_hook;
	callbacks.ObjectFree = &jvmti_callback_object_free_hook;
	callbacks.VMStart = &jvmti_callback_vm_start_hook;
	callbacks.VMInit = &jvmti_callback_vm_init_hook;
	callbacks.VMDeath = &jvmti_callback_vm_death_hook;
	callbacks.ThreadEnd = &jvmti_callback_thread_end_hook;

	error = (*jvmti_env)->SetEventCallbacks(
			jvmti_env, &callbacks, (jint) sizeof(callbacks)
	);
	check_jvmti_error(jvmti_env, error, "Cannot set callbacks");


	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set class load hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_CLASS_PREPARE, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set class prepare hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_OBJECT_FREE, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set object free hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_VM_START, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set jvm start hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_VM_INIT, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set jvm init hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_VM_DEATH, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set jvm death hook");

	error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
			JVMTI_EVENT_THREAD_END, NULL);
	check_jvmti_error(jvmti_env, error, "Cannot set thread end hook");

	error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "buffids",
			&to_buff_lock);
	check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");

	error = (*jvmti_env)->CreateRawMonitor(jvmti_env, "obj free",
			&obj_free_lock);
	check_jvmti_error(jvmti_env, error, "Cannot create raw monitor");

	// init blocking queues
	bq_create(jvmti_env, &utility_q, BQ_UTILITY, sizeof(process_buffs *));
	bq_create(jvmti_env, &empty_q, BQ_BUFFERS, sizeof(process_buffs *));
	bq_create(jvmti_env, &objtag_q, BQ_BUFFERS, sizeof(process_buffs *));
	bq_create(jvmti_env, &send_q, BQ_BUFFERS + BQ_UTILITY, sizeof(process_buffs *));

	// allocate buffers and add to the empty and utility buffer queues
	int i;
	for(i = 0; i < BQ_BUFFERS + BQ_UTILITY; ++i) {

		process_buffs * pb = &(pb_list[i]);

		// allocate process_buffs

		// allocate space for buffer struct
		pb->analysis_buff = malloc(sizeof(buffer));
		// allocate buffer
		buffer_alloc(pb->analysis_buff);

		// allocate space for buffer struct
		pb->command_buff = malloc(sizeof(buffer));
		// allocate buffer
		buffer_alloc(pb->command_buff);

		if(i < BQ_BUFFERS) {
			// add buffer to the empty queue
			_buffs_release(pb);
		}
		else {
			// add buffer to the utility queue
			_buffs_utility_release(pb);
		}
	}

	// initialize total ordering buff array
	for(i = 0; i < TO_BUFFER_COUNT; ++i) {
		to_buff_array[i].pb = NULL;
	}

	tagger_init(jvm, jvmti_env);
	// start sending thread
  sender_connect(options);


	return 0;
}
