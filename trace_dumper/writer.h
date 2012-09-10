/*
 * writer.h
 *
 *  Created on: Aug 8, 2012
 *      Original Author: Yotam Rubin
 *      Maintainer:		 Yitzik Casapu, Infinidat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#ifndef WRITER_H_
#define WRITER_H_

#include <sys/uio.h>
#include "trace_dumper.h"

int total_iovec_len(const struct iovec *iov, int iovcnt);
int write_single_record(struct trace_dumper_configuration_s *conf, struct trace_record_file *record_file, const struct trace_record *rec);
int trace_dumper_write(struct trace_dumper_configuration_s *conf, struct trace_record_file *record_file, const struct iovec *iov, int iovcnt, bool_t dump_to_parser);
int dump_iovector_to_parser(const struct trace_dumper_configuration_s *conf, struct trace_parser *parser, const struct iovec *iov, int iovcnt);

/* A wrapper for trace_dumper_write which syncs the written data to the disk after the write. */
int trace_dumper_write_to_record_file(struct trace_dumper_configuration_s *conf, struct trace_record_file *record_file, int iovcnt);

/* If the existing size of the IO vector is insufficient to hold size_t records, increase it by at least 50% */
struct iovec *increase_iov_if_necessary(struct trace_record_file *record_file, size_t required_size);

static inline bool_t record_file_should_be_parsed(
		const struct trace_dumper_configuration_s *conf,
		const struct trace_record_file *record_file)
{
	return conf->record_file.fd == record_file->fd;
}

#endif /* WRITER_H_ */
