/*
 * util.c
 *
 * Utility functions for sws.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <magic.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#include <time.h>

#include "net.h"
#include "util.h"

#ifdef sun
#include <strings.h>
#endif

/**
 * Writes log data to a file descriptor passed to program.
 * Takes the values in struct logging and writes to fd in the
 * format provided in the man page
 */
int
writelog(int fd, struct logging* log)
{
  char logstr[sizeof(struct logging) + 16];
  int n = 0;
  snprintf(logstr, sizeof(logstr), "%s [%s] \"%s\" %s %s\n", log->remoteip,
      log->request_time, log->request_lineq, log->request_status,
      log->response_size);
  if ((n = write(fd, logstr, strlen(logstr))) < 0) {
    perror("Write error");
    exit(EXIT_FAILURE);
  }
  return 0;
}

/*
 * Initializes the flags and parameters to default values.
 *
 * @param flag the flags to initialize.
 */
void
flags_init(struct flags *flag)
{
  assert(flag != NULL);
  flag->c_dir = NULL;
  flag->dflag = 0;
  flag->i_address = NULL;
  flag->ipv6 = 0;
  flag->lflag = 0;
  flag->l_log_file = NULL;
  flag->p_port = DEFAULT_PORT;
  flag->dir = NULL;
  flag->logfd = 0;
}

/*
 * Checks whether the given string refers to an existing directory.
 *
 * @param dir the path to check
 * @return 1, if dir is an existing directory. Otherwise, 0.
 */
int
is_dir(const char *dir)
{
  struct stat sb;

  if (dir == NULL) {
    warnx("the provided dir is NULL");
    return 0;
  }
  if (stat(dir, &sb) < 0) {
    warn("cannot stat dir %s", dir);
    return 0;
  }
  if (!S_ISDIR(sb.st_mode)) {
    warnx("path %s you provided is not a directory", dir);
    return 0;
  }

  return 1;
}

/**
 * Fills buffer buffer and returns -1 on error. Otherwise, the number of bytes
 * read are returned.
 *
 * @param buf the buffer to fill
 * @param buf_len the length of the buffer
 * @param fd the file descriptor to read from
 * @return the number of bytes read or -1 if an error occurred.
 */
int
read_buffer(char *buf, size_t buf_len, int fd)
{
  int rval;
  int bytes_read;

  assert(buf != NULL);

  bytes_read = 0;

  do {
    if ((rval = read(fd, buf + bytes_read, buf_len - bytes_read)) < 0) {
      return -1;
    } else {
      bytes_read += rval;
    }
  } while ((rval != 0) && (bytes_read < buf_len));

  return bytes_read;
}

/**
 * Writes to a buffer and terminates the string with a null byte.
 *
 * @param buf the buffer to write to.
 * @param buf_size the size of the buffer in bytes.
 * @param format the format string to write to the buffer.
 * @return the number of bytes written excluding the null byte on success.
 * Otherwise, -1.
 */
int
write_buffer(char * buf, size_t buf_size, const char * format, ...)
{
  int written;
  va_list args;

  va_start(args, format);
  written = vsnprintf(buf, buf_size, format, args);
  va_end(args);

  if ((written < 0) || (written >= buf_size)) {
    return -1;
  } else {
    return written;
  }
}

/*
 * Signal handler for the server.
 *
 * @param signo the signal number of the signal to handle.
 */
void
server_sig_handler(int signo)
{
  int status;

  switch (signo) {
  case SIGCHLD:
    /* reap child */
    if (wait(&status) < 0) {
      perror("wait");
    }
    break;
  default:
    errx(EXIT_FAILURE, "do not know how to handle signal number %d", signo);
    break;
  }
}

/**
 * Converts the given time from localtime to GMT.
 * @param time the time to convert from local to GMT.
 * @return -1, if an error occured. The converted time otherwise.
 */
time_t
local_to_gmtime(time_t * time)
{
  struct tm result;

  if (gmtime_r(time, &result) == NULL) {
    return -1;
  }
  return mktime(&result);
}

/**
 * Parses the HTTP-date according to RFC 1945 and stores it as a time_t.
 *
 * @param date the date to parse
 * @param dst where to store the time_t
 * @return 0 on success. Otherwise, -1.
 */
int
http_date_to_time(const char * date, time_t * dst)
{
  char * pos;
  struct tm tm;
  time_t time;

  if (date == NULL) {
    return -1;
  }

  bzero(&tm, sizeof(tm));

  /*
   * Possible formats:
   *
   * rfc1123-date   = wkday "," SP date1 SP time SP "GMT"
   * rfc850-date    = weekday "," SP date2 SP time SP "GMT"
   * asctime-date   = wkday SP date3 SP time SP 4DIGIT
   */
  if ((pos = strchr(date, ',')) == NULL) {
    /* parse asctime-date */
    if (strptime(date, "%a %b %e %H:%M:%S %Y", &tm) == NULL) {
      return -1;
    }
  } else {
    int weekday_len = pos - date;

    if (weekday_len == 3) {
      /* rfc1123-date */
      if (strptime(date, "%a, %d %b %Y %H:%M:%S GMT", &tm) == NULL) {
        return -1;
      }
    } else {
      /* rfc850-date */
      if (strptime(date, "%A, %d-%b-%y %H:%M:%S GMT", &tm) == NULL) {
        return -1;
      }
    }
  }

  if ((time = mktime(&tm)) == -1) {
    return -1;
  } else {
    *dst = time;
  }

  return 0;
}

/**
 * Converts the given time to a string in RFC 1123 format.
 *
 * @param time the time to convert to a RFC 1123 date
 * @param dst the buffer to fill with the HTTP date string
 * @param dst_len the size of the buffer in bytes
 * @return 0 on success. Otherwise, -1.
 */
int
time_to_http_date(time_t *time, char * dst, size_t dst_len)
{
  struct tm date;

  if (time == NULL) {
    return -1;
  }
  /* Use GMT time */
  if (gmtime_r(time, &date) == NULL) {
    return -1;
  }

  /* use time format from RFC 1123 */
  if (strftime(dst, dst_len, "%a, %d %b %Y %H:%M:%S GMT", &date) == 0) {
    return -1;
  }

  return 0;
}

/** 
 * Get MIME type/subtype for given file path.
 *
 * @param file name
 * @param dst the buffer to fill with the mime type string
 * @param dst_len the size of the buffer in bytes
 */
void
mime_type(const char * path, char * dst, size_t dst_len)
{
  magic_t magic;
  const char * mime_type;

  if (path == NULL) {
    return;
  }
  if ((magic = magic_open(MAGIC_MIME_TYPE)) == NULL) {
    warnx("%s", magic_error(magic));
    return;
  }
  if (magic_load(magic, NULL) != 0) {
    warnx("magic_load failed");
  }
  if ((mime_type = magic_file(magic, path)) == NULL) {
    warnx("%s", magic_error(magic));
    magic_close(magic);
    return;
  }
  bzero(dst, dst_len);
  /* make sure there is space for terminating null byte*/
  if (dst_len < (strlen(mime_type) + 1)) {
    warnx("insufficient buffer size for MIME type");
    return;
  }
  strncpy(dst, mime_type, dst_len - 1);
  magic_close(magic);
}
/*
 * Returns a null terminated string containing a line of input read from a socket.
 * it supports lines terminated in CRLF "\r\n" as well as just LF '\n'
 * 
 * socket - socket to be read
 * buf - bufer that will be filled with the line of input;
 * buf_size - size of the buffer to be filled
 *
 * return value - size of the returned string or -1 on failure. The max return value
 *  will be buf_size-1 the '\0' character is not counted.
 */

int
get_socket_line(int socket, char * buf, size_t buf_size)
{
  char a;
  int chars_read, count = 0;

  /* just in case user is trying to be "funny" */
  if (buf_size == 0) {
    return count;
  }
  if (buf_size == 1) {
    buf[count] = '\0';
    return count;
  }

  /*
   * read socket character by character and add them to buf until new line
   * character is encountered or until the end of the buffer is reached
   */
  while ((chars_read = read(socket, &a, 1)) > 0) {
    if (a == '\n') {
      break;
    }

    buf[count++] = a;

    if (count == buf_size - 1) {
      break;
    }
  }

  /* read returns -1 on failure */
  if (chars_read < 0) {
    /* return value indicating failure user can check errno for details */
    return chars_read;
  }

  /* if first character read is '\n' */
  if (count == 0) {
    buf[count] = '\0'; /* empty string */
    return count;
  }
  /*
   * Discard carriage return character if present and add '\0' character
   * to the end of the string
   */
  if (buf[count - 1] == '\r') {
    buf[--count] = '\0';
  } else {
    buf[count] = '\0';
  }
  return count;
}
