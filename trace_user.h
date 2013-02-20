/***
Copyright 2012 Yotam Rubin <yotamrubin@gmail.com>
   Sponsored by infinidat (http://infinidat.com)
   
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
***/

#ifndef __TRACE_USER_H__
#define __TRACE_USER_H__

#include "trace_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void TRACE__fini(void);// __attribute__((destructor));

#ifndef __repr__    
#define __repr__ TRACE_REPR_INTERNAL_METHOD_NAME ()
#endif
    
typedef unsigned char hex_t;
#ifdef __cplusplus
#define HEX_REPR(x, size) reinterpret_cast<const hex_t (*)[size]>(x)
#else
#define HEX_REPR(x, size) (hex_t (*)[size]) (x)
#endif

#define TRACE_INT_AS_HEX(x) HEX_REPR(&(x), sizeof(x))

/* Function instrumentation */
#define NO_INSTRUMENT __attribute__((no_instrument_function))
#define INSTRUMENTED __attribute__((no_instrument_function))

/* Allow the meaning of __attribute__((no_instrument_function)) to be reversed if the user wants no-instrumentation as the default for
   a particular file
*/
#define ENTIRE_FILE_NO_FUNC_TRACE static const int __traces_file_no_instrument = 1;

#ifndef __has_attribute         
  #define __has_attribute(x) 0
#endif

/* The following macros are detected by the source-code preprocessor and converted to calls to dumper functions. */
    
#ifdef __TRACE_INSTRUMENTATION
#define CALL_INVALID __attribute__((error("traces: Trace symbol name should not appear in final code, this is a bug. Contact Yotam Rubin <yotamrubin@gmail.com> and report a bug")))
#ifdef __cplusplus

void TRACE_REPR_CALL_NAME (...) CALL_INVALID;

#define TRACE_SEV_X(ignored, sev) void sev(...) CALL_INVALID;

TRACE_SEVERITY_DEF

#undef TRACE_SEV_X
    
#else

void TRACE_REPR_CALL_NAME () CALL_INVALID;

#define TRACE_SEV_X(ignored, sev) void sev() CALL_INVALID;

TRACE_SEVERITY_DEF

#undef TRACE_SEV_X

#endif /* __cplusplus */


/* The code generated by the trace instrumentor makes use of variable length array in both C and C++, so don't warn against them. */
#pragma GCC diagnostic ignored "-Wvla"


#include "trace_lib.h"

#else /* __TRACE_INSTRUMENTATION */
#ifdef __cplusplus
void TRACE_REPR_CALL_NAME (...);

#define TRACE_SEV_X(ignored, sev) void sev(...);

TRACE_SEVERITY_DEF

#undef TRACE_SEV_X

#else
void TRACE_REPR_CALL_NAME ();

#define TRACE_SEV_X(ignored, sev) void sev();

TRACE_SEVERITY_DEF

#undef TRACE_SEV_X

#endif /* __cplusplus */
#endif /* __TRACE_INSTRUMENTATION */    
#ifdef __cplusplus
}
#endif

#endif 
