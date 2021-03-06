/*
 * validator.h
 *
 *  Created on: Oct 25, 2012

 ***
Copyright 2012 infinidat Inc
   Written by Yitzik Casapu of Infinidat

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

#ifndef _TRACE_VALIDATOR_H_
#define _TRACE_VALIDATOR_H_

#include "trace_defs.h"

enum trace_validator_flag {
    TRACE_VALIDATOR_FIX_ERRORS = 0x01,
    TRACE_VALIDATOR_SKIP_PID_CONSISTENCY_CHECK = 0x10,
};

/* Validator function signature
 * records - an array of trace records
 * n_records - the number of records.
 * flags - A logical or of values from enum trace_validator_flag defined above.
 * Return values:
 * 		If an unrecoverable error was found a negative value is returned and errno is set to EPROTO.
 * 		Otherwise the number of recoverable errors found is returned.
 *  */
typedef int (*trace_post_write_validator)(struct trace_record *records, int n_records, unsigned flags, void *context);

/* Validators for particular formats */
int trace_dump_validator(struct trace_record *records, int n_records, unsigned flags, void *context);
int trace_typed_record_sequence_validator(struct trace_record *records, int n_records, unsigned flags, void *context);

#endif /* _TRACE_VALIDATOR_H_ */
