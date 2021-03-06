/***
Copyright 2012 Yotam Rubin <yotamrubin@gmail.com>
   Sponsored by infinidat (http://infinidat.com)

   Modified and maintained by Yitzik Casapu of Infinidat - 2012

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

#ifndef TRACE_DUMPER_H_
#define TRACE_DUMPER_H_


#include "../platform.h"

#include <stdio.h>
#include <dirent.h>     /* for MANE_MAX */
#include <sys/uio.h>
#include <aio.h>
#include <sys/types.h>

#include "../bool.h"
#include "../trace_metadata_util.h"
#include "../min_max.h"
#include "../trace_macros.h"
#include "../array_length.h"
#include "../trace_lib.h"
#include "../validator.h"
#include "../list_template.h"

#define METADATA_IOVEC_SIZE 2*(MAX_METADATA_SIZE/TRACE_RECORD_PAYLOAD_SIZE+1)

// The threshold stands at about 60 MBps
#define OVERWRITE_THRESHOLD_PER_SECOND (1000000)
#define RELAXATION_BACKOFF (TRACE_SECOND * 10)

struct trace_mapped_metadata {
    struct iovec *metadata_iovec;
    struct trace_record metadata_payload_record;
    unsigned long log_descriptor_count;
    unsigned long type_definition_count;
    size_t size;
    size_t metadata_iovec_len;
    const struct trace_metadata_region *base_address;
    struct trace_log_descriptor *descriptors;
};

struct trace_mapped_records {
    volatile struct trace_record *records;
    volatile struct trace_records_mutable_metadata *mutab;
    struct trace_records_immutable_metadata *imutab;

    trace_record_counter_t current_read_record;
    unsigned int last_flush_offset;

    trace_ts_t 	 next_flush_ts;
    trace_record_counter_t next_flush_record;
    unsigned int next_flush_offset;

    trace_record_counter_t num_records_discarded_by_dumper;
    trace_record_counter_t num_records_discarded_by_process;
    trace_record_counter_t unreported_traces_discard_by_process;
    trace_ts_t trace_discard_by_process_last_reprorted_time;

    struct trace_record buffer_dump_record;
};

#define TRACE_BUFNAME_LEN NAME_MAX
#define MAX_BUFFER_COUNT (10)

struct trace_mapped_buffer {
    char name[TRACE_BUFNAME_LEN];
    struct trace_buffer *records_buffer_base_address;
    trace_record_counter_t records_buffer_size;
    trace_record_counter_t last_metadata_offset;
    bool_t metadata_dumped;
    bool_t notification_metadata_dumped;
    struct trace_mapped_records *mapped_records;
    struct trace_mapped_metadata metadata;
    pid_t pid;
    int n_record_buffers;
    bool_t dead;
    trace_ts_t process_time;
};

#define TRACE_METADATA_IOVEC_SIZE  (2*(MAX_METADATA_SIZE/TRACE_RECORD_PAYLOAD_SIZE+1))

#define TRACE_PREFERRED_FILE_MAX_RECORDS_PER_FILE        0x1000000
#define PREFERRED_NUMBER_OF_TRACE_HISTORY_FILES (7)
#define TRACE_PREFERRED_MAX_RECORDS_PER_LOGDIR        (TRACE_PREFERRED_FILE_MAX_RECORDS_PER_FILE) * PREFERRED_NUMBER_OF_TRACE_HISTORY_FILES;
#define TRACE_FILE_MAX_RECORDS_PER_CHUNK       0x10000
#define TRACE_FILE_IMMEDIATE_FLUSH_THRESHOLD	(TRACE_FILE_MAX_RECORDS_PER_CHUNK / 2)
#define TRACE_FILE_DESIRED_RECORD_PER_DUMP     (TRACE_FILE_IMMEDIATE_FLUSH_THRESHOLD / 4)

struct trace_output_mmap_info;  /* See writer.h for its full definition */
struct trace_record_io_timestamps {
	trace_ts_t started_memcpy;
	trace_ts_t finished_memcpy;
	trace_ts_t started_validation;
	trace_ts_t finished_validation;
};

struct trace_internal_buf;
struct trace_record_file;

typedef int (*trace_async_write_completion)(struct trace_record_file *record_file, struct aiocb *cb);

struct trace_record_file {
    trace_record_counter_t records_written;  /* Records actually written to the underlying file */
    char filename[NAME_MAX];
    int fd;
    struct trace_output_mmap_info *mapping_info;
    struct trace_internal_buf *internal_buf;
    trace_record_counter_t records_discarded;
    struct iovec *iov;
    unsigned iov_allocated_len;
    unsigned iov_count;
    struct aiocb async_writes[1];
    trace_async_write_completion async_completion_routine;  /* Routine to be called when Asynchronous disk writes complete */
    trace_post_write_validator   post_write_validator;      /* Used to validate trace data after it has been copied to the dumper's internal storage. */
    unsigned validator_flags_override;
    void *validator_context;
    int validator_last_result;
    FILE *perf_log_file;
    struct trace_record_io_timestamps ts;
};


struct trace_post_event_actions {
    const char *on_file_close;
};

/* Values for the request_flags field of struct trace_dumper_configuration_s below */
enum trace_request_flags {
	TRACE_REQ_CLOSE_RECORD_FILE = 0x01,
	TRACE_REQ_CLOSE_NOTIFICATION_FILE = 0x02,
	TRACE_REQ_CLOSE_RECORD_TIMING_FILE = 0x04,
	TRACE_REQ_CLOSE_NOTIFICATION_TIMING_FILE = 0x08,
	TRACE_REQ_CLOSE_ALL_FILES = TRACE_REQ_CLOSE_RECORD_FILE | TRACE_REQ_CLOSE_NOTIFICATION_FILE | TRACE_REQ_CLOSE_RECORD_TIMING_FILE | TRACE_REQ_CLOSE_NOTIFICATION_TIMING_FILE,

	TRACE_REQ_RENAME_RECORD_FILE = 0x10,
	TRACE_REQ_RENAME_NOTIFICATION_FILE = 0x20,
	TRACE_REQ_RENAME_ALL_FILES = TRACE_REQ_RENAME_RECORD_FILE | TRACE_REQ_RENAME_NOTIFICATION_FILE,

	TRACE_REQ_DISCARD_ALL_BUFFERS = 0x100,

	TRACE_REQ_RECORD_OPS = TRACE_REQ_CLOSE_RECORD_FILE | TRACE_REQ_RENAME_RECORD_FILE,
	TRACE_REQ_NOTIFICATION_OPS = TRACE_REQ_CLOSE_NOTIFICATION_FILE | TRACE_REQ_RENAME_NOTIFICATION_FILE,
	TRACE_REQ_ALL_OPS = -1,
};

enum operation_type {
    OPERATION_TYPE_DUMP_RECORDS,
    OPERATION_TYPE_DUMP_BUFFER_STATS,
};


CREATE_LIST_PROTOTYPE(MappedBuffers, struct trace_mapped_buffer, 100);
/* Note: The number of mapped buffers sets an upper limit to the number of processes from which we can simultaneously collect traces. */

typedef char buffer_name_t[0x100];
CREATE_LIST_PROTOTYPE(BufferFilter, buffer_name_t, 20);

CREATE_LIST_PROTOTYPE(PidList, trace_pid_t, MappedBuffers_NUM_ELEMENTS);


struct trace_dumper_configuration_s {
    const char *logs_base;
    const char *notifications_subdir;
    pid_t  attach_to_pid;
    struct trace_post_event_actions post_event_actions;
    unsigned int request_flags;
    unsigned int header_written;
    unsigned int write_to_file;
    unsigned int write_notifications_to_file;
    unsigned int dump_online_statistics;
    const char *fixed_output_filename;
    const char *fixed_notification_filename;
    unsigned int syslog;
    unsigned int log_details;
    bool_t		 log_performance_to_file;
    bool_t	     low_latency_write;
    unsigned     compression_algo;
    size_t       internal_buf_size;
    trace_ts_t   new_dump_max_interval;   /* Maximum time interval between "dump" records (if there is data) */
    trace_ts_t 	 start_time;
    unsigned int no_color_specified;
    unsigned int color;
    enum trace_severity minimal_notification_severity;
    enum trace_severity minimal_allowed_severity;
    trace_ts_t next_possible_overwrite_relaxation;
    trace_ts_t last_overwrite_test_time;
    trace_record_counter_t last_overwrite_test_record_count;

    const char *quota_specification;
    long long max_records_per_logdir;
    trace_record_counter_t max_records_per_file;
    trace_record_counter_t max_records_per_second;
    trace_record_counter_t max_records_pending_write_via_mmap;
    bool_t stopping;
	struct trace_record_file record_file;
	struct trace_record_file notification_file;
	unsigned int last_flush_offset;
    enum operation_type op_type;
    trace_ts_t prev_flush_ts;
    trace_ts_t next_flush_ts;
    trace_ts_t ts_flush_delta;  /* Polling interval used by the main thread */
    trace_ts_t async_io_wait;   /* Timeout when trying to obtain an async IO CB */
    trace_ts_t next_stats_dump_ts;
    trace_ts_t next_housekeeping_ts;

    /* Parameters used to size and time calls to posix_fadvise() in low-latency mode */
    trace_ts_t max_flush_interval;
    size_t     preferred_flush_bytes;

    BufferFilter filtered_buffers;
    MappedBuffers mapped_buffers;
    PidList dead_pids;
    struct iovec flush_iovec[1 + MAX_BUFFER_COUNT *
                             (2 * __TRACE_STDC_MIN(TRACE_RECORD_BUFFER_RECS, TRACE_DEFAULT_RECORD_BUFFER_RECS) / 2 +
                                  __TRACE_STDC_MIN(TRACE_RECORD_BUFFER_FUNCS_RECS, TRACE_DEFAULT_RECORD_BUFFER_RECS) / 2)];
};


#endif /* TRACE_DUMPER_H_ */
