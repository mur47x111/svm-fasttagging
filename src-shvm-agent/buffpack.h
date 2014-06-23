#ifndef _BUFFPACK_H
#define	_BUFFPACK_H

#if defined (__APPLE__) && defined (__MACH__)

#include <machine/endian.h>

#if BYTE_ORDER == BIG_ENDIAN
#define htobe64(x) (x)
#else // BYTE_ORDER != BIG_ENDIAN
#define htobe64(x) __DARWIN_OSSwapInt64((x))
#endif

#else // !(__APPLE__ && __MACH__)
#include <endian.h>
#endif

// Disabled check to make it compile under OS X with clang/LLVM.
//#ifndef __STDC_IEC_559__
//#error "Requires IEEE 754 floating point!"
//#endif

#include <stdint.h>
#include <jvmti.h>

#include "buffer.h"

// interpret bytes differently
union float_jint {
	float f;
	jint i;
};

// interpret bytes differently
union double_jlong {
	double d;
	jlong l;
};

void pack_boolean(buffer * buff, jboolean to_send);

void pack_byte(buffer * buff, jbyte to_send);

void pack_char(buffer * buff, jchar to_send);

void pack_short(buffer * buff, jshort to_send);

void pack_int(buffer * buff, jint to_send);

void pack_long(buffer * buff, jlong to_send);

void pack_float(buffer * buff, jfloat to_send);

void pack_double(buffer * buff, jdouble to_send);

void pack_string_utf8(buffer * buff, const void * string_utf8,
		uint16_t size_in_bytes);

void pack_bytes(buffer * buff, const void * data, jint size);

#endif	/* _BUFFPACK_H */
