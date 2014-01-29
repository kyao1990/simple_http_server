/*
 * util.h
 *
 *  Created on: Nov 2, 2013
 *      Author: pluto
 */

#ifndef _SWS_UTIL_H_
#define _SWS_UTIL_H_

#include <time.h>

#define FLAGS_SUPPORTED "c:dhi:l:p:"
#define BUF_SIZE (4 * 1024)


/**
 * The logging structure is used to store the data if logging is enabled
 * and then written to a file provided by the user
 *
 */
struct logging
{
  char remoteip[64];
  char request_time[128];
  char request_lineq[BUF_SIZE];
  char request_status[16];
  char response_size[64];
};

struct flags
{
  const char *c_dir;
  int dflag;
  const char *i_address;
  int ipv6;
  int lflag;
  const char *l_log_file;
  unsigned int p_port;
  const char *dir;
  int logfd;
};

int
writelog(int fd, struct logging*);
void
flags_init(struct flags *);
int
is_dir(const char *);
int
read_buffer(char *, size_t, int);
int
write_buffer(char *, size_t, const char *, ...);
void
server_sig_handler(int);
time_t
local_to_gmtime(time_t *);
int
http_date_to_time(const char *, time_t *);
int
time_to_http_date(time_t *, char *, size_t);
void
mime_type(const char *, char *, size_t);
int
get_socket_line(int, char*, size_t);
#endif /* _SWS_UTIL_H_ */
