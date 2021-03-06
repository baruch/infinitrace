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


#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sysexits.h>
#include "../list_template.h"
#include "../bool.h"
#include "../trace_metadata_util.h"
#include "../min_max.h"
#include "../array_length.h"
#include "../trace_clock.h"
#include "../trace_proc_util.h"
#include <syslog.h>
#include <time.h>
#include <sys/mman.h>
#include <assert.h>
#include "../trace_lib.h"
#include "../trace_user.h"
#include "../trace_str_util.h"
#include "../snappy/snappy.h"
#include "trace_dumper.h"
#include "filesystem.h"
#include "sgio_util.h"
#include "internal_buffer.h"
#include "write_prep.h"
#include "writer.h"
#include "buffers.h"
#include "init.h"
#include "open_close.h"
#include "metadata.h"
#include "housekeeping.h"


// Start compression code

#define ROUND_UP(num, divisor) ((divisor) * (((num) + (divisor) - 1) / (divisor)))
#define ROUND_DOWN(num, divisor) ((num) - (num) % (divisor))


static trace_record_counter_t adjust_for_overrun(struct trace_mapped_records *mapped_records)
{
	trace_record_counter_t current_record = mapped_records->mutab->current_record;
	TRACE_ASSERT(current_record >= mapped_records->imutab->max_records - 1);

	trace_record_counter_t additional_skipped;

	for (additional_skipped = 1;	/* TODO: Consider a higher starting value to avoid the new starting point getting overrun. */
		(0 == (mapped_records->records[(current_record + additional_skipped) & mapped_records->imutab->max_records_mask].termination & TRACE_TERMINATION_FIRST));
		additional_skipped++) ;

	mapped_records->current_read_record = current_record + additional_skipped - mapped_records->imutab->max_records;
	return additional_skipped;

	/* TODO: There's a race condition here is that remains to be addressed. Until the records are
	 * actually written to the disk, the writing process will continue to write records. This will result in
	 * some records appearing twice.
	 * A safer solution is required in the longer term */
}

static void mark_records_as_written(const struct trace_mapped_records *mapped_records)
{
    if (mapped_records->next_flush_record <= mapped_records->current_read_record) {
        TRACE_ASSERT(mapped_records->next_flush_record == mapped_records->current_read_record);
        return;
    }

    const unsigned mask = mapped_records->imutab->max_records_mask;
    TRACE_ASSERT((mask & (mask + 1)) == 0);

    for (trace_record_counter_t i = mapped_records->current_read_record; i < mapped_records->next_flush_record; i++) {
        /* Set the severity field of each trace record to TRACE_SEV_INVALID, a.k.a 0. We do so in a way that (hopefully) doesn't
         * pollute the CPU cache using an intrinsic (uses SSE2 on x86-64). In order to accomplish this we have to set the entire
         * aligned 32-bit word containing the severity field to 0 */
        struct trace_record *const rec = (struct trace_record *) (mapped_records->records) + (i & mask);
        const uintptr_t bit_fields_aligned_p = (uintptr_t) (rec->bit_fields) - offsetof(struct trace_record, bit_fields) % sizeof(int);
        write_int_to_ptr_uncached((int *) bit_fields_aligned_p, TRACE_COMPILE_TIME_VERIFY_IS_ZERO(TRACE_SEV_INVALID));
    }
    __sync_synchronize();
}

static void advance_mapped_record_counters(struct trace_dumper_configuration_s *conf)
{
    int i;
    int rid;
    struct trace_mapped_buffer *mapped_buffer = NULL;
    struct trace_mapped_records *mapped_records = NULL;

	for_each_mapped_records(i, rid, mapped_buffer, mapped_records) {
		mapped_records->mutab->latest_flushed_ts = mapped_records->next_flush_ts;

        mark_records_as_written(mapped_records);
        mapped_records->current_read_record = mapped_records->next_flush_record;
		mapped_records->last_flush_offset = mapped_records->next_flush_offset;
		mapped_records->mutab->next_flush_record = mapped_records->current_read_record;
	}
}

static void reset_discarded_record_counters(struct trace_dumper_configuration_s *conf)
{
    int i;
    int rid;
    struct trace_mapped_buffer *mapped_buffer = NULL;
    struct trace_mapped_records *mapped_records = NULL;

	for_each_mapped_records(i, rid, mapped_buffer, mapped_records) {
		mapped_records->num_records_discarded_by_dumper = 0;
	}

	conf->record_file.records_discarded = 0;
}

static int write_records_to_internal_buffer(struct trace_dumper_configuration_s *conf)
{
    const unsigned int num_iovecs = conf->record_file.iov_count;
    unsigned iovecs_processed = 1;
    unsigned i;
    const struct iovec *const iov = conf->flush_iovec;
    struct trace_internal_buf *const buf = conf->record_file.internal_buf;
    unsigned actual_records_written = 0;
    struct trace_record *dump_header = NULL;

    for (i = 0; i < num_iovecs; i += iovecs_processed) {
        const struct trace_record *const rec = trace_record_from_iov(iov + i);
        iovecs_processed = 1;  /* default */
        switch(rec->rec_type) {
        case TRACE_REC_TYPE_BUFFER_CHUNK: {
            TRACE_ASSERT((i + 1 < num_iovecs) && (TRACE_RECORD_SIZE == iov[i].iov_len));
            struct trace_record *const chunk_header = internal_buf_write_recs(buf, rec, 1);
            if (NULL == chunk_header) {
                goto rollback_on_err;
            }

            if (rec->u.buffer_chunk.flags & TRACE_CHUNK_HEADER_FLAG_COMPRESSED_ANY) {
                TRACE_ASSERT((rec->u.buffer_chunk.flags & TRACE_CHUNK_HEADER_FLAG_COMPRESSED_ANY) == conf->compression_algo);
                const struct iovec *const chunk_content_iov = iov + i + 1;
                const unsigned n_record_bufs = max_iovecs_fitting_size(chunk_content_iov, num_iovecs - i - 1, TRACE_RECORD_SIZE * rec->u.buffer_chunk.records);
                TRACE_ASSERT(TRACE_RECORD_SIZE * rec->u.buffer_chunk.records == total_iovec_len(chunk_content_iov, n_record_bufs));

                const ssize_t num_compressed_bytes = internal_buf_write_compressed(buf, chunk_content_iov, n_record_bufs);
                if (num_compressed_bytes < 0) {
                    goto rollback_on_err;
                }

                const ssize_t num_compressed_recs = internal_buf_bytes_to_recs(num_compressed_bytes);
                actual_records_written += num_compressed_recs;
                iovecs_processed += n_record_bufs;
                chunk_header->u.buffer_chunk.records = num_compressed_recs;
                chunk_header->u.buffer_chunk.slack_bytes = TRACE_RECORD_SIZE * num_compressed_recs - num_compressed_bytes;
                TRACE_ASSERT(chunk_header->u.buffer_chunk.slack_bytes < TRACE_RECORD_SIZE);
            }

            actual_records_written += 1;
            break;
        }

        case TRACE_REC_TYPE_DUMP_HEADER:
            TRACE_ASSERT((0 == i) && (NULL == dump_header) && (TRACE_RECORD_SIZE == iov[i].iov_len));
            TRACE_ASSERT((num_iovecs >= 3) &&
                    (TRACE_REC_TYPE_BUFFER_CHUNK == trace_record_from_iov(iov + 1)->rec_type) &&
                    (TRACE_REC_TYPE_TYPED        == trace_record_from_iov(iov + 2)->rec_type));
            dump_header = internal_buf_write_recs(buf, rec, 1);
            if (NULL == dump_header) {
                goto rollback_on_err;
            }
            TRACE_ASSERT(iovecs_processed == 1);
            actual_records_written += 1;
            break;

        case TRACE_REC_TYPE_UNKNOWN: {
            const trace_ts_t ts = rec->ts;
            DEBUG("Encountered a record of type unknown", ts, rec->pid, rec->tid, rec->severity);
        }
            /* no break - handle as a TRACE_REC_TYPE_TYPED record */

        case TRACE_REC_TYPE_TYPED: {
            if (i > 0) {
                const struct trace_record *const presumed_chunk = trace_record_from_iov(iov + i - 1);
                TRACE_ASSERT((presumed_chunk->rec_type != TRACE_REC_TYPE_BUFFER_CHUNK) || (0 == (presumed_chunk->u.buffer_chunk.flags & TRACE_CHUNK_HEADER_FLAG_COMPRESSED_ANY)));
            }
            TRACE_ASSERT(0 == (iov[i].iov_len % TRACE_RECORD_SIZE));
            const size_t n_recs = iov[i].iov_len / TRACE_RECORD_SIZE;

            if (NULL == internal_buf_write_recs(buf, rec, n_recs)) {
                goto rollback_on_err;
            }
            TRACE_ASSERT(iovecs_processed == 1);
            actual_records_written += n_recs;
        }
            break;

            /* TODO: Consider handling metadata by inserting a special indicator that instructs the writer thread to write metadata */

        default:
            TRACE_ASSERT(0);
            break;
        }
    }

    if (actual_records_written > 0) {
        if (NULL != dump_header) {
            dump_header->u.dump_header.total_dump_size = actual_records_written - 1;
        }

        internal_buf_commit_write(buf);
    }
    return actual_records_written;

rollback_on_err:
    internal_buf_rollback_write(buf);
    if (ENOMEM == errno) {
        DEBUG("Not enough space in the internal buffer only had space for", actual_records_written, buf->read_idx.c, buf->write_idx.c, buf->n_slack_recs.c);
    }
    else {
        ERR("FAILED record writing to internal buffer! errno=", errno, strerror(errno));
    }
    return -1;
}

static bool_t should_write_records_now(const struct trace_dumper_configuration_s *conf, unsigned int total_written_records, trace_ts_t cur_ts, bool_t force_flush)
{
    return force_flush || (cur_ts >= conf->prev_flush_ts + conf->new_dump_max_interval) || (total_written_records >= TRACE_FILE_DESIRED_RECORD_PER_DUMP);
}

static int possibly_write_iovecs_to_disk(struct trace_dumper_configuration_s *conf, unsigned int total_written_records, trace_ts_t cur_ts, bool_t force_flush)
{
    unsigned int num_iovecs = conf->record_file.iov_count;
    int bytes_written = 0;

    if ((num_iovecs > 1) && should_write_records_now(conf, total_written_records, cur_ts, force_flush)) {
    	TRACE_ASSERT(num_iovecs >= 3);  /* Should have at least dump and chunk headers and some data */
        conf->last_flush_offset = conf->record_file.records_written;
		conf->prev_flush_ts = cur_ts;
		conf->next_flush_ts = cur_ts + conf->ts_flush_delta;

		const struct iovec *const iov = conf->flush_iovec;

	    if (conf->compression_algo) {
	        bytes_written = write_records_to_internal_buffer(conf);
	    }
	    else {
            const struct trace_record *const dump_header = trace_record_from_iov(iov);
            TRACE_ASSERT((dump_header->rec_type != TRACE_REC_TYPE_DUMP_HEADER) || (dump_header->u.dump_header.total_dump_size + 1 == total_written_records));

            bytes_written = trace_dumper_write(conf, &conf->record_file, iov, num_iovecs);
            if ((unsigned int)bytes_written != (total_written_records * sizeof(struct trace_record))) {
                if (bytes_written < 0) {
                    if (EAGAIN != errno) {
                        ERR("Unexpected error while writing records, errno=", errno, strerror(errno));
                        syslog(LOG_ERR|LOG_USER, "Had error %s (%d) while writing records", strerror(errno), errno);
                        return -1;
                    }
                    else {
                        int i;
                        int rid;
                        struct trace_mapped_buffer *mapped_buffer = NULL;
                        struct trace_mapped_records *mapped_records = NULL;

                        for_each_mapped_records(i, rid, mapped_buffer, mapped_records) {
                            if (mapped_records->current_read_record != mapped_records->next_flush_record) {
                                TRACE_ASSERT(mapped_records->current_read_record <= mapped_records->next_flush_record);
                                trace_record_counter_t n_discarded_records = mapped_records->next_flush_record - mapped_records->current_read_record;
                                WARN("Trace dumper has had to discard records due to insufficient buffer space for pid", mapped_buffer->pid, mapped_buffer->name, rid, n_discarded_records);
                                mapped_records->num_records_discarded_by_dumper += n_discarded_records;
                            }
                        }

                        /* We had to discard records, but since we did so in a controlled manner with proper reporting we return success. */
                    }
                }
                else {
                    ERR("Wrote fewer bytes than requested:", bytes_written, "Actual records written:", bytes_written / (int)sizeof(struct trace_record), total_written_records);
                    syslog(LOG_ERR|LOG_USER, "Wrote only %d records out of %u requested", (bytes_written / (int)sizeof(struct trace_record)), total_written_records);
                    /* TODO: Consider "rewinding" the file in this case. */

                    if (0 == errno) {
                        errno = ETIMEDOUT;
                    }
                    return -1;
                }

            }
	    }
	}

    if ((bytes_written >= 0) || ((ENOMEM == errno) && (conf->internal_buf_size > 0))) {
        conf->record_file.iov_count = 0;
        if (bytes_written > 0) {
            advance_mapped_record_counters(conf);
        }
    }
    else {
        ERR("Not advancing counters due to write error, errno=", errno, strerror(errno));
    }

    return MIN(bytes_written, 0);
}

/* Return TRUE if the given mapped records could include records with severity equal or greater than the given severity */
static inline bool_t mapped_records_may_exceed_severity(const struct trace_mapped_records *mapped_records, enum trace_severity sev)
{
    return (1U << sev) <= mapped_records->imutab->severity_type;
}

static void possibly_report_record_loss(
		const struct trace_dumper_configuration_s *conf,
		const struct trace_mapped_buffer *mapped_buffer,
		const struct trace_mapped_records *mapped_records,
		const struct records_pending_write *deltas)
{
	unsigned records_pos = mapped_records - mapped_buffer->mapped_records;

	TRACE_ASSERT(records_pos < TRACE_BUFFER_NUM_RECORDS);
	TRACE_ASSERT(deltas->lost >= 0);
	TRACE_ASSERT(deltas->remaining_before_loss >= 0);

	if (deltas->lost > 0) {
		syslog(LOG_USER|LOG_WARNING, "Trace dumper has lost %ld records while writing traces from area %u of %s (pid %d) to file %s.",
				deltas->lost, records_pos, mapped_buffer->name, mapped_buffer->pid, conf->record_file.filename);
	}
	else {
		const double remaining_percent_threshold = 5.0;
		double remaining_percent = deltas->remaining_before_loss * 100.0 / mapped_records->imutab->max_records;
		if (remaining_percent < remaining_percent_threshold) {
			syslog(LOG_USER|LOG_WARNING,
					"Trace dumper's remaining space in buffer area %u for %s (pid %d) has dropped to %.1f%% while writing to to file %s.",
					records_pos, mapped_buffer->name, mapped_buffer->pid, remaining_percent, conf->record_file.filename);
		}
	}
}

static void report_record_loss(struct trace_record_file *record_file)
{
    if (record_file->validator_last_result > 0) {
        int n_records_indavlidated = record_file->validator_last_result;
        const char *filename = record_file->filename;
        WARN("Had to invalidate records which had been overrun", n_records_indavlidated, filename);
        syslog( LOG_USER|LOG_WARNING,
                "Trace dumper has had to invalidate %d record(s) from file %s which had been overrun while writing.",
                n_records_indavlidated, filename);
        record_file->validator_last_result = 0;
    }
}

static int write_notification_records(struct trace_dumper_configuration_s *conf)
{
    /* TODO: Implement a more clever validator that takes level thresholds into account. */
    conf->notification_file.post_write_validator = trace_typed_record_sequence_validator;
    conf->notification_file.validator_context = NULL;
    int rc = 0;

    if (trace_dumper_write_to_record_file(conf, &conf->notification_file) < 0) {
        ERR("Error", errno, strerror(errno), "writing records to the file", conf->notification_file.filename);
        if (conf->notification_file.validator_last_result < 0) {
            ERR("Unrecoverable error while validating records to the notification file",
                    conf->notification_file.filename, conf->notification_file.validator_last_result);
            syslog( LOG_USER|LOG_WARNING,
                    "Trace dumper's data validation found an unrecoverable error with code %d while writing notifications to the file %s",
                    conf->notification_file.validator_last_result, conf->notification_file.filename);
        }

        rc = -1;
    }

    if ((rc >= 0) && (conf->notification_file.validator_last_result > 0)) {
        report_record_loss(&conf->notification_file);
        rc = conf->notification_file.validator_last_result;
    }

    conf->notification_file.iov_count = 0;
    return rc;
}

static bool_t should_dump_metadata(struct trace_dumper_configuration_s *conf)
{
    struct trace_mapped_buffer *mapped_buffer = NULL;
    int i = 0;
    for_each_mapped_buffer(i, mapped_buffer) {
        if (metadata_dumping_needed(conf, mapped_buffer)) {
            return TRUE;
        }
    }

    return FALSE;
}

/* Iterate over all the mapped buffers and flush them. The result will be a new dump added to the file, with the
 * records flushed from every buffer that has data pending constituting a chunk.
 * A negative return value indicates an error. A non-negative return value indicates the total number of records still pending
 * writing after the buffer flush */
static int trace_flush_buffers(struct trace_dumper_configuration_s *conf)
{
    struct trace_mapped_buffer *mapped_buffer = NULL;
    struct trace_mapped_records *mapped_records = NULL;
    struct trace_record dump_header_rec;
    struct iovec *iovec;
    unsigned int num_iovecs = 0;
    int i = 0, rid = 0;
    unsigned int total_written_records = 0;
    unsigned int total_warn_iovecs = 0;
    int total_unflushed_records = 0;
    long lost_records = 0L;
    trace_record_counter_t min_remaining = TRACE_RECORD_BUFFER_RECS;

	const trace_ts_t cur_ts = get_nsec_monotonic();
	init_dump_header(conf, &dump_header_rec, cur_ts, &iovec, &num_iovecs, &total_written_records);


	for_each_mapped_records(i, rid, mapped_buffer, mapped_records) {
		struct trace_record_buffer_dump *bd = NULL;
		const struct trace_record *last_rec = NULL;
		struct records_pending_write deltas;
        lost_records = 0;
        
        if (! mapped_records_may_exceed_severity(mapped_records, conf->minimal_allowed_severity)) {
            WARN("Not dumping pid", mapped_buffer->pid, "with severity type", mapped_records->imutab->severity_type, "due to overwrite");
            continue;
        }
        
        calculate_delta(mapped_records, &deltas);
        total_unflushed_records += deltas.beyond_chunk_size;

        lost_records = deltas.lost;
        if (lost_records) {
        	lost_records += adjust_for_overrun(mapped_records);
        	calculate_delta(mapped_records, &deltas);
        	deltas.lost += lost_records;
        }
        else if (0 == deltas.total) {
            continue;
        }
        
        init_buffer_chunk_record(
        		conf, mapped_buffer, mapped_records,
        		&bd, &iovec, &num_iovecs,
        		&deltas, cur_ts, total_written_records);

		if (deltas.from_buf_start) {
			iovec = &conf->flush_iovec[num_iovecs++];
			iovec->iov_base = (void *)&mapped_records->records[0];
			iovec->iov_len = TRACE_RECORD_SIZE * deltas.from_buf_start;
			last_rec = (const struct trace_record *) &mapped_records->records[deltas.from_buf_start - 1];
		}
		else {
			last_rec = (const struct trace_record *) (&mapped_records->records[
			                    (mapped_records->imutab->max_records_mask & mapped_records->current_read_record) + deltas.up_to_buf_end - 1]);
		}


		TRACE_ASSERT(	(TRACE_TERMINATION_LAST & last_rec->termination) ||
				/* Make sure this is not due to record buffer overflow overwriting *last_rec */
				(mapped_records->mutab->current_record - mapped_records->current_read_record >= deltas.total));


		/* Note: there's a possible race condition here that could lead to silent record loss
		 * if *last_rec gets overwritten by incoming data before we retrieve the ts from it. */
		mapped_records->next_flush_ts     = last_rec->ts;
		mapped_records->next_flush_record = mapped_records->current_read_record + deltas.total;

        possibly_report_record_loss(conf, mapped_buffer, mapped_records, &deltas);
        min_remaining = MIN(min_remaining, (trace_record_counter_t)(deltas.remaining_before_loss));

        if (conf->write_notifications_to_file && mapped_records_may_exceed_severity(mapped_records, conf->minimal_notification_severity)) {
            unsigned int num_warn_iovecs = add_warn_records_to_iov(mapped_records, deltas.total, conf->minimal_notification_severity, &conf->notification_file);
			if (num_warn_iovecs > 0) {
			    write_notification_records(conf);
			    total_warn_iovecs += num_warn_iovecs;
			}
        }

		total_written_records += deltas.total + 1;
	}

    dump_header_rec.u.dump_header.total_dump_size = total_written_records - 1;
    trace_dumper_update_written_record_count(&conf->record_file);
    TRACE_ASSERT(trace_dumper_get_effective_record_file_pos(&conf->record_file) >= conf->record_file.records_written);
    dump_header_rec.u.dump_header.first_chunk_offset =  trace_dumper_get_effective_record_file_pos(&conf->record_file) + 1;

    conf->record_file.post_write_validator = trace_dump_validator;
    conf->record_file.validator_context = NULL;
    conf->record_file.iov_count = num_iovecs;

    if (possibly_write_iovecs_to_disk(conf, total_written_records, cur_ts, total_warn_iovecs > 0) < 0) {
        TRACE_ASSERT(0 != errno);
        if ((ENOMEM != errno) || (NULL == conf->record_file.internal_buf)) {  /* Any error other than no space in internal buffer */
            WARN("Writing records did not complete successfully. errno=", errno, num_iovecs, total_written_records, cur_ts);
        }
        return -1;
    }

    report_record_loss(&conf->record_file);

    /* The latest chunk written has successfully output information about any discarded records, so reset the counters. */
    reset_discarded_record_counters(conf);

    return total_unflushed_records;
}



static bool_t dumping_should_stop(struct trace_dumper_configuration_s *conf)
{
	if (conf->attach_to_pid && !has_mapped_buffers(conf) && !trace_process_exists(conf->attach_to_pid)) {
		conf->stopping = TRUE;
	}

	return conf->stopping;
}

static int dump_records(struct trace_dumper_configuration_s *conf)
{
    int rc = 0;
    bool_t file_creation_err = FALSE;
    TRACE_ASSERT(0 == internal_buf_num_recs_pending(conf->record_file.internal_buf));
    while (! dumping_should_stop(conf)) {
        rc = rotate_trace_file_if_necessary(conf);
        if (0 != rc) {
        	syslog(LOG_USER|LOG_ERR, "trace_dumper: Error %s encountered while rotating trace files.", strerror(errno));
        	file_creation_err = TRUE;
            break;
        }

        rc = open_trace_file_if_necessary(conf);
        if (rc != 0) {
        	syslog(LOG_USER|LOG_ERR, "trace_dumper: Error %s encountered while opening the trace file.", strerror(errno));
        	file_creation_err = TRUE;
        	break;
        }
        
        possibly_dump_online_statistics(conf);
        if (! should_dump_metadata(conf)) {
            rc = trace_flush_buffers(conf);
            if (rc > TRACE_FILE_IMMEDIATE_FLUSH_THRESHOLD) {
                struct timespec ts;
                trace_init_timespec(&ts, conf->ts_flush_delta);
                TRACE_ASSERT(0 == TEMP_FAILURE_RETRY(nanosleep(&ts, &ts)));
                continue;
            }
            else if ((rc < 0) && (ENOMEM != errno)) {
                syslog(LOG_USER|LOG_ERR, "trace_dumper: Error %s encountered while flushing trace buffers.", strerror(errno));
                break;
            }
        }

        rc = do_housekeeping_if_necessary(conf);
        if (EAGAIN == rc) { /* No housekeeping performed */
			usleep(1000);
		}
        else if (rc < 0) {
            ERR("Error while performing housekeeping functions. errno=", errno, strerror(errno));
        	break;
        }
        else TRACE_ASSERT(0 == rc);
    }

    discard_all_buffers_immediately(conf);

    if (rc < 0) {
		rc = errno;
		syslog(LOG_USER|LOG_ERR, "trace_dumper: Error encountered while writing traces: %s.", strerror(rc));
		ERR("Unexpected failure writing trace file:", rc, strerror(rc));
		switch (rc) {
		case ENOMEM:
			return EX_SOFTWARE;

		case ENAMETOOLONG:
			return EX_USAGE;

		default:
			break;
		}

		if (file_creation_err) {
			return EX_CANTCREAT;
		}

		return EX_IOERR;
    }

    return 0;
}


static int op_dump_records(struct trace_dumper_configuration_s *conf)
{
    int rc;

    rc = attach_and_map_buffers(conf);
    if (0 != rc) {
        return EX_NOINPUT;
    }

    conf->start_time = trace_get_walltime_ns();
    return dump_records(conf);
}

static int op_dump_stats(struct trace_dumper_configuration_s *conf)
{
    int rc;

    rc = attach_and_map_buffers(conf);
	if (0 != rc) {
		return EX_NOINPUT;
	}

    dump_online_statistics(conf);
    return 0;
}

static int run_dumper(struct trace_dumper_configuration_s *conf)
{
    switch (conf->op_type) {
    case OPERATION_TYPE_DUMP_RECORDS:
        return op_dump_records(conf);

    case OPERATION_TYPE_DUMP_BUFFER_STATS:
        return op_dump_stats(conf);

    default:
        break;
    }

    return 0;
}


int main(int argc, char **argv)
{
    struct trace_dumper_configuration_s *conf = trace_dumper_get_configuration();
    
    if (0 != parse_commandline(conf, argc, argv)) {
    	print_usage(argv[0]);
        return EX_USAGE;
    }

    if (!conf->write_to_file && !conf->dump_online_statistics) {
            fprintf(stderr, "%s: Must specify either -w or -v\n", argv[0]);
            print_usage(argv[0]);
            return EX_USAGE;
    }

    int rc = init_dumper(conf);
    if (0 != rc) {
    	if (EX_USAGE == rc) {
    		print_usage(argv[0]);
    	}
    	else {
    		fprintf(stderr, "%s failed to start with error code %d (see sysexits.h for its meaning)\n", argv[0], rc);
    	}
        return rc;
    }
    
    if (0 != set_signal_handling()) {
    	fprintf(stderr, "Error encountered while trying to set signal handlers: %s\n", strerror(errno));
    	return EX_OSERR;
    }

    rc = run_dumper(conf);
    if (0 != rc) {
    	fprintf(stderr,
    		"Error encountered while writing traces: %s, please see the syslog for more details.\nExiting with error code %d (see sysexits.h for its meaning)\n",
    		strerror(errno), rc);
    }
    return rc;
}
