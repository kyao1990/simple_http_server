/*
 * http.c - implementation of the HTTP/1.0 server functionality
 * as specified in RFC 1945: http://www.ietf.org/rfc/rfc1945.txt
 * Copyright (c) 2013
 * Project Team Geronimo
 * Stevens Institute of Technology.
 *
 * Implementation of a simple HTTP 1.0 web server named sws.
 */

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <fcntl.h>

#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#ifndef _SVID_SOURCE
#define _SVID_SOURCE
#include <dirent.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "http.h"
#include "net.h"
#include "util.h"

#ifdef __linux__
#include <bsd/stdlib.h>
#include <bsd/string.h>
#endif

#ifndef __NetBSD__
#include <alloca.h>
#endif

#ifdef sun
#include <strings.h>
#endif

#ifndef LOGIN_NAME_MAX
#ifdef _POSIX_LOGIN_NAME_MAX
#define LOGIN_NAME_MAX _POSIX_LOGIN_NAME_MAX
#else
#define LOGIN_NAME_MAX 9
#endif
#endif

#define BUF_SIZE (4 * 1024)
#define CRLF "\r\n"
#define HTTP_VERSION "HTTP/1.0"
#define HTTP_VERSION_1 "HTTP/1.0"
#define HTTP_VERSION_09 "HTTP/0.9"
#define HTTP_VERSION_11 "HTTP/1.1"
#define SERVER_ID "sws/1.0"

#define IF_MODIFIED_SINCE_PREFIX "If-Modified-Since:"
#define CONTENT_LENGTH_PREFIX    "Content-Length:"
#define CONTENT_TYPE_PREFIX      "Content-Type:"

#define INDEX_HTML "index.html"
#define CGI_PREFIX "/cgi-bin/"

static void
init_request(struct request *);
static int
set_entity_body_headers(struct response *, const char *);
static int
coderesp_headers(struct response *, int);

static void
init_logging(struct logging* l)
{
  bzero(l->remoteip, sizeof(l->remoteip));
  bzero(l->request_time, sizeof(l->request_time));
  bzero(l->request_lineq, sizeof(l->request_lineq));
  bzero(l->request_status, sizeof(l->request_status));
  bzero(l->response_size, sizeof(l->response_size));
}

/**
 * Sets the code field of the given response to the given value.
 * Other response response fields are set to default values.
 *
 * @param response the response to initialize.
 * @param code the response code number to set.
 */
void
init_response(struct response * response, int code)
{
  response->code = code;
  response->content_length = 0;
  bzero(response->content_type, sizeof(response->content_type));
  response->last_modified = -1;
}

/**
 * Initializes the given request.
 *
 * @param request the request to initialize
 */
static void
init_request(struct request * request)
{
  request->method = -1;
  request->version_major = -1;
  request->version_minor = -1;
  request->if_modified_since_date = -1;
  request->content_length = -1;
  bzero(request->content_type, sizeof(request->content_type));
  bzero(request->path, sizeof(request->path));
  bzero(request->querystring, sizeof(request->querystring));
}

/**
 * Function receives a socket pointer after a connection has been
 * established successfully.
 * Parses the input from the client for valid syntax using string tokens.
 * Calls response function with correct code.
 * Calls the file server function with pathname of file to serve.
 *
 * @param socket server socket that is connected to a client.
 * @param flag user-provided flags.
 * @param client_ip the IP of the client in IPv6 format
 * @return 0 on success. Otherwise, -1 is returned.
 */
int
httpd(int socket, struct flags * flag, const char * client_ip)
{
  char buf[BUF_SIZE];
  int bytes_read;
  int remain_buf;
  struct request newreq;
  char * token[BUF_SIZE];
  int token_count = 0;
  struct response response;
  struct logging log;
  int http_status = 0;
  char realpath_str[PATH_MAX + 1];
  char * request_line;
  char * header_line;
  int header_parsing_failed = 0;
  int simple_request = 0;
  int cgi_request = 0; /* to be set by checkuri call */
  int serve_file = 0;
  /* initialize newreq */
  init_request(&newreq);
  /*initialize log struct*/
  init_logging(&log);

  /* leave space for terminating null byte */
  remain_buf = sizeof(buf) - 1;
  bzero(buf, sizeof(buf));

  do {
    /* Wait for request from client with timeout. */
    wait_for_data(socket);
    if ((bytes_read = read(socket, buf + (sizeof(buf) - 1 - remain_buf),
        remain_buf)) < 0) {
      perror("Reading stream message");
      return -1;
    } else if (bytes_read == 0) {
      break;
    } else {
      remain_buf -= bytes_read;
    }
  } while (strstr(buf, CRLF CRLF) == NULL);

  if (strstr(buf, CRLF CRLF) == NULL) {
    init_response(&response, RESPONSE_STATUS_BAD_REQUEST);
    return send_generic_page(&response, 0, socket, NULL);
  } else {
    int if_modified_since_prefix_len = strlen(IF_MODIFIED_SINCE_PREFIX);
    int content_length_prefix_len = strlen(CONTENT_LENGTH_PREFIX);
    int content_type_prefix_len = strlen(CONTENT_TYPE_PREFIX);
    int failure_status = 0;
    time_t current = time(NULL);

    request_line = strtok(buf, CRLF);

    /*Save data to log to log*/
    strncpy(log.remoteip, client_ip, sizeof(log.remoteip) - 1);
    strncpy(log.request_lineq, request_line, sizeof(log.request_lineq) - 1);
    time_to_http_date(&current, log.request_time, sizeof(log.request_time));

    header_line = strtok(NULL, CRLF);

    while (header_line != NULL) {
      if (strlen(header_line) > if_modified_since_prefix_len) {
        if (strncasecmp(header_line, IF_MODIFIED_SINCE_PREFIX,
            if_modified_since_prefix_len) == 0) {
          int time_len;
          char *date;
          size_t white_spaces = 0;
          char *current_char = header_line + if_modified_since_prefix_len;

          /* Skip whitespaces */
          while (isspace((int)*current_char)) {
            current_char++;
            white_spaces++;
          }

          /* leave space for terminating null byte */
          time_len = strlen(header_line) - if_modified_since_prefix_len
              - white_spaces + 1;
          date = (char *) alloca(time_len);
          strncpy(date,
              header_line + if_modified_since_prefix_len + white_spaces,
              time_len - 1);
          date[time_len - 1] = 0;

          if (http_date_to_time(date, &newreq.if_modified_since_date) < 0) {
            header_parsing_failed = 1;
            break;
          }
        }
      }
      if (strlen(header_line) > content_length_prefix_len + 2) {
        if (strncasecmp(header_line, CONTENT_LENGTH_PREFIX,
            content_length_prefix_len) == 0) {
          newreq.content_length = atoi(
              &(header_line[content_length_prefix_len + 1]));
        }
      }
      if (strlen(header_line) > content_type_prefix_len + 2) {
        if (strncasecmp(header_line, CONTENT_TYPE_PREFIX,
            content_type_prefix_len) == 0) {
          snprintf(newreq.content_type, 64, "%s",
              &(header_line[content_type_prefix_len + 1]));
        }
      }
      /*Next token*/
      header_line = strtok(NULL, CRLF);
    }

    /* tokenize token based on space */
    token[token_count] = strtok(request_line, " ");
    token_count++;
    while ((token[token_count] = strtok(NULL, " ")) != NULL) {
      token_count++;
    }

    if (token_count == 2) {
      simple_request = 1;
      newreq.version_major = 0;
      newreq.version_minor = 9;
    } else {
      newreq.version_major = 1;
      newreq.version_minor = 0;
    }

    /* if less than 3 tokens and not 0.9 send error */
    if ((header_parsing_failed) || ((token_count != 3) && !simple_request)) {
      init_response(&response, RESPONSE_STATUS_BAD_REQUEST);
    }
    /*Check HTTP 1.0 version token against supported version */
    else if ((strncasecmp(token[token_count - 1], HTTP_VERSION_1, 8) != 0)
        && !simple_request) {
      init_response(&response, RESPONSE_STATUS_VERSION_NOT_SUPPORTED);
    }
    /*compare with supported methods on first token*/
    else if (strcasecmp(token[0], "GET") == 0) {
      newreq.method = REQUEST_METHOD_GET;
      strcpy(newreq.path, token[1]);
      checkuri(&newreq, &http_status, flag, realpath_str, &cgi_request);
      init_response(&response, http_status);
      if (http_status == RESPONSE_STATUS_OK) {
        strcpy(newreq.path, realpath_str);
      }
    } else if ((strcasecmp(token[0], "HEAD") == 0) && !simple_request) {
      newreq.method = REQUEST_METHOD_HEAD;
      strcpy(newreq.path, token[1]);
      checkuri(&newreq, &http_status, flag, realpath_str, &cgi_request);
      init_response(&response, http_status);
      if (http_status == RESPONSE_STATUS_OK) {
        strcpy(newreq.path, realpath_str);
      }
    } else if ((strcasecmp(token[0], "POST") == 0) && !simple_request) {
      newreq.method = REQUEST_METHOD_POST;
      if (flag->c_dir == NULL) { /* POST IS ONLY VALID IF CGI IS ENABLED */
        init_response(&response, RESPONSE_STATUS_BAD_REQUEST);
      } else {
        strcpy(newreq.path, token[1]);
        checkuri(&newreq, &http_status, flag, realpath_str, &cgi_request);
        if (!cgi_request && http_status == RESPONSE_STATUS_OK) {
          init_response(&response, RESPONSE_STATUS_BAD_REQUEST);
        } else {
          init_response(&response, http_status);
          strcpy(newreq.path, realpath_str);
        }
      }
    } else {
      init_response(&response, RESPONSE_STATUS_NOT_IMPLEMENTED);
    }

    /* send file when GET and OK*/
    serve_file = (newreq.method == REQUEST_METHOD_GET)
        && (response.code == RESPONSE_STATUS_OK) && (!cgi_request);

    /* TODO check cgi_request flag and handle CGI request */
    if ((response.code == RESPONSE_STATUS_OK)
        && ((newreq.method == REQUEST_METHOD_GET)
            || (newreq.method == REQUEST_METHOD_HEAD)) && (!cgi_request)) {
      set_entity_body_headers(&response, realpath_str);
    }

    /* TODO check cgi_request flag and handle CGI request */
    if (response.code == RESPONSE_STATUS_OK) {
      /* fileserver generates own header response. Otherwise, respond with
       * headers. */
      if (!serve_file) {
        failure_status = coderesp(&response, socket, !simple_request);
      }

      if (cgi_request) {
        failure_status = execute_cgi(&newreq, flag, &http_status, realpath_str,
            socket);
      } else if (serve_file) {
        failure_status = fileserver(&newreq, &response, simple_request, socket,
            flag);
      }
    } else {
      /* POST is only valid if CGI is enabled */
      if (newreq.method == REQUEST_METHOD_POST && flag->c_dir == NULL) {
        failure_status = send_generic_page(&response, simple_request, socket,
            "CGI is not enabled in the server");
      } else if (newreq.method == REQUEST_METHOD_POST && !cgi_request) {
        failure_status = send_generic_page(&response, simple_request, socket,
            "The uri suplied with the POST resource must point to a CGI");
      } else {
        failure_status = send_generic_page(&response, simple_request, socket,
            NULL);
      }
    }

    /* Save response code to log and print log*/
    snprintf(log.request_status, sizeof(log.request_status), "%d",
        response.code);
    snprintf(log.response_size, sizeof(log.response_size), "%d",
        response.content_length);

    if (flag->dflag) {
      (void) writelog(STDOUT_FILENO, &log);
    } else if (flag->lflag) {
      (void) writelog(flag->logfd, &log);
    }

    return failure_status;
  }
  /*TODO work through other tokens for error conditions*/

  return 0;
}

/**
 * Stats the file at the given path and sets the corresponding fields
 * in the given response. The fields to set are the entity body header fields
 * Content-Type, Content-Length, Last-Modified.
 *
 * @param response the response to augment with stat information.
 * @param path the path to the file to stat.
 * @return 0 on success. Otherwise, -1 is returned.
 */
static int
set_entity_body_headers(struct response * response, const char * path)
{
  struct stat sb;

  if (path == NULL) {
    warnx("cannot set entity body headers for NULL path");
    return -1;
  }
  if (stat(path, &sb) < 0) {
    perror("stat");
    return -1;
  } else {
    mime_type(path, response->content_type, sizeof(response->content_type));
    response->content_length = sb.st_size;
    response->last_modified = sb.st_mtime;

    return 0;
  }
}

/**
 * Sends the given response information to the client.
 *
 * @param response the response to send to the client.
 * @param socket the socket on the server to which the client is connected.
 * @return 0 on success. Otherwise, -1 is returned.
 */
static int
coderesp_headers(struct response *response, int socket)
{
  char buf[BUF_SIZE];
  time_t t;
  char http_date[128];
  int retval;
  size_t buf_size_remain;
  int written;
  char *buf_pos;

  buf_size_remain = sizeof(buf);
  buf_pos = buf;

  /* get current time */
  t = time(NULL);
  retval = time_to_http_date(&t, http_date, sizeof(http_date));

  /* Write Date field */
  if (retval < 0) {
    warnx("failed to convert time to http date");
    return -1;
  }

  written = write_buffer(buf_pos, buf_size_remain, "Date: %s%s", http_date,
  CRLF);
  if (written < 0) {
    warnx("failed to write to buffer");
    return -1;
  } else {
    buf_pos += written;
    buf_size_remain -= written;
  }

  /* Write Server field */
  written = write_buffer(buf_pos, buf_size_remain, "Server: %s%s", SERVER_ID,
  CRLF);
  if (written < 0) {
    warnx("failed to write to buffer");
    return -1;
  } else {
    buf_pos += written;
    buf_size_remain -= written;
  }

  /* Write Last-Modified field */
  if (response->last_modified != -1) {
    char http_date[128];

    int retval = time_to_http_date(&response->last_modified, http_date,
        sizeof(http_date));
    if (retval < 0) {
      warnx("failed to convert time to HTTP date");
      return -1;
    }
    written = write_buffer(buf_pos, buf_size_remain, "Last-Modified: %s%s",
        http_date, CRLF);
    if (written < 0) {
      warnx("failed to write to buffer");
      return -1;
    } else {
      buf_pos += written;
      buf_size_remain -= written;
    }
  }

  /* Write Content-Type field */
  if (strlen(response->content_type) > 0) {
    written = write_buffer(buf_pos, buf_size_remain, "Content-Type: %s%s",
        response->content_type, CRLF);
    if (written < 0) {
      warnx("failed to write to buffer");
      return -1;
    } else {
      buf_pos += written;
      buf_size_remain -= written;
    }
  }

  /* Write Content-Length field */
  if (response->content_length >= 0) {
    written = write_buffer(buf_pos, buf_size_remain, "Content-Length: %d%s",
        response->content_length, CRLF);
    if (written < 0) {
      warnx("failed to write to buffer");
      return -1;
    } else {
      buf_pos += written;
      buf_size_remain -= written;
    }
  }

  /* Write one empty line at the end of the header response */

  written = write_buffer(buf_pos, buf_size_remain, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer");
    return -1;
  } else {
    buf_pos += written;
    buf_size_remain -= written;
  }

  /* Write full header response to socket */
  if (write(socket, buf, sizeof(buf) - buf_size_remain) < 0) {
    warn("write failed");
    return -1;
  } else {
    return 0;
  }
}

/**
 * Called by the httpd function with an int for the code per RFC 1945.
 * Uses the socket pointer passed to generate a response to client
 * and send this to the client over the socket.
 *
 * @param response the response fields as defined in RFC 1945.
 * @param socket server socket that is connected to a client.
 * @param full_response 1 if a full response should be returned. 0 for a simple
 *   response.
 * @return 0 on success. Otherwise, -1 is returned.
 */
int
coderesp(struct response * response, int socket, int full_response)
{
  char buf[BUF_SIZE];
  size_t buf_size;
  int written;
  int code;

  if (!full_response) {
    /* Do not respond with headers for simple requests (HTTP/0.9 messages) */
    return 0;
  }

  buf_size = sizeof(buf);
  code = response->code;

  /* Send response code to client */
  switch (code) {
  case RESPONSE_STATUS_OK:
    written = write_buffer(buf, buf_size, "%s %d OK%s", HTTP_VERSION, code,
    CRLF);
    break;

  case RESPONSE_STATUS_BAD_REQUEST:
    written = write_buffer(buf, buf_size, "%s %d Bad Request%s", HTTP_VERSION,
        code, CRLF);
    break;

  case RESPONSE_STATUS_FORBIDDEN:
    written = write_buffer(buf, buf_size, "%s %d Forbidden%s",
    HTTP_VERSION, code, CRLF);
    break;

  case RESPONSE_STATUS_NOT_FOUND:
    written = write_buffer(buf, buf_size, "%s %d Not Found%s",
    HTTP_VERSION, code, CRLF);
    break;

  case RESPONSE_STATUS_NOT_IMPLEMENTED:
    written = write_buffer(buf, buf_size, "%s %d Not Implemented%s",
    HTTP_VERSION, code, CRLF);
    break;

  case RESPONSE_STATUS_VERSION_NOT_SUPPORTED:
    written = write_buffer(buf, buf_size, "%s %d Version Not Supported%s",
    HTTP_VERSION, code, CRLF);
    break;

  case RESPONSE_STATUS_CONNECTION_TIMED_OUT:
    written = write_buffer(buf, buf_size, "%s %d Connection Timed Out%s",
    HTTP_VERSION, code, CRLF);
    break;

  default:
    code = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    /* If unknown code is passed, it is an internal server error. */
    /* FALLTHROUGH */
  case RESPONSE_STATUS_INTERNAL_SERVER_ERROR:
    written = write_buffer(buf, buf_size, "%s %d Internal Server Error%s",
    HTTP_VERSION, code, CRLF);
    break;
  }

  if (written < 0) {
    warnx("failed to write to buffer");
    return -1;
  } else {
    if (write(socket, buf, written) < 0) {
      warn("write failed");
      return -1;
    } else {
      /* Send headers to client */
      int result;

      result = coderesp_headers(response, socket);
      /* terminate process after timeout */
      if (code == RESPONSE_STATUS_CONNECTION_TIMED_OUT) {
        warn("Connection Timed Out\n");
        close(socket);
        exit(EXIT_SUCCESS);
      } else {
        return result;
      }
    }
  }
}

/**
 * Called by the httpd function with a pathname requested and socket.
 * Checks for a valid pathname that does not break out of the web directory.
 * Stats file for properties and handles request according to this result.
 * Opens the file and sends the contents of the file to the client.
 *
 * @param pathname the requested pathname as provided by the client.
 * @param socket server socket that is connected to a client.
 * @param simple_response 1 if no headers are to be sent. 0 otherwise.
 * @param flag user-provided flags.
 * @return 0 on success. Otherwise, -1 is returned.
 */
int
fileserver(struct request * request, struct response * response,
    int simple_response, int socket, struct flags * flag)
{
  int fd;
  struct stat st_stat;
  int n_bytes;
  char buf[BUF_SIZE];

  if (stat(request->path, &st_stat) != 0) {
    perror("stat");
    init_response(response, RESPONSE_STATUS_INTERNAL_SERVER_ERROR);
    return send_generic_page(response, simple_response, socket, NULL);
  }

  /* check if file needs to be delivered. */
  if ((request->if_modified_since_date != -1)
      && (request->if_modified_since_date >= local_to_gmtime(&st_stat.st_mtime))) {
    /* file is not new enough. */
    response->content_length = 0;
    bzero(response->content_type, sizeof(response->content_type));
    return coderesp(response, socket, !simple_response);
  }

  if (coderesp(response, socket, !simple_response) != 0) {
    warnx("failed to write response headers");
    return -1;
  }

  if (!S_ISDIR(st_stat.st_mode)) {
    /* open file as read only */
    if ((fd = open(request->path, O_RDONLY)) < 0) {
      perror("open");
      /* log open failure */
      return -1;
    }

    while ((n_bytes = read(fd, buf, BUF_SIZE)) > 0) {
      if (write(socket, buf, n_bytes) != n_bytes) {
        perror("write");
        /* log write error */
        return -1;
      }
    }

    if (n_bytes < 0) {
      perror("read");
      /* log read error */
      return -1;
    }

    /* Done writing file */
    return 0;
  } else /* uri is a directory */{
    /* send directory listing */
    if (send_directory_listing(request, socket) < 0) {
      warnx("error sending directory listing");
      return -1;
    }
  }

  return 0;
}

/**
 * Checks that the given URI exist in the system.
 * If the resource exist, it checks the access permission and if it is
 * within the allowed directory for serving files or CGI scripts
 *
 * @param path is the path (uri) requested by the client
 * @param uri_status, int * to be changed by the function
 *  after execution the value of uri_status will be the appropriate HTTP
 *  response to the client 200 OK, 403 Forbidden, 404 Not Found, etc.
 *  It can be set to NULL if not required
 * @param flag user-provided flags.
 * @param realpath_str path where the requested file is located on the server.
 * @return 0 on success (equivalent to 200 OK). Otherwise -1.
 */
int
checkuri(struct request * request, int * uri_status, struct flags * flag,
    char * realpath_str, int * cgi_request)
{
  /* TODO: DELETE struct stat st_stat; */
  char uri_path[PATH_MAX + 1];
  char uri_real_path[PATH_MAX + 1];
  char server_real_path[PATH_MAX + 1];
  char query_string[PATH_MAX + 1];
  char * username;
  struct passwd *pw;
  int mode;
  char *chp;
  /*
   * realpath returns the path without /./ or /../ or symlinks
   */

  /* check if uri points to a user's home directory in the form /~username */
  if (request->path[0] == '/' && request->path[1] == '~') {
    /* extract username portion */
    const char * userdir = request->path + 2;
    int i;
    for (i = 0; i < strlen(userdir); i++) {
      if (userdir[i] == '/') {
        break;
      }
    }

    /* check for valid username length */
    if (i > LOGIN_NAME_MAX) {
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
      }
      return -1;
    }

    username = malloc(i + 1);

    (void) strncpy(username, userdir, i);
    username[i] = '\0';

    /* at this point we have a userid and we get their home directory */

    if ((pw = getpwnam((const char *) username)) == NULL) {
      free(username);
      /* couldn't find username in password file, /etc/passwd */
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_NOT_FOUND;
      }
      return -1;
    }
    free(username);

    /* server realpath for this request will be the user's home directory */
    if (realpath(pw->pw_dir, server_real_path) == NULL) {
      /*log error*/
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_NOT_FOUND;
        /*
         * Bad server configuration since this should have been checked at
         * startup
         */
      }
      return -1;
    }

    /* create realpath for requested uri */
    userdir = userdir + i; /* include '/' if present */

    if ((strlen(server_real_path) + strlen(userdir)) > PATH_MAX) {
      /*log error*/
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
        /*
         * Bad server configuration since this should have been checked at
         * startup
         */
      }
      return -1;
    }

    if ((strcpy(uri_path, server_real_path) == NULL)
        || (strcat(uri_path, userdir) == NULL)) {
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
      }
      return -1;
      /* log error */
    }
    /* check if requested uri begins with /cgi-bin/ and c flag was specified */
  } else if (strstr(request->path, "/cgi-bin/") == request->path
      && flag->c_dir != NULL) { /* replace /cgi-bin/ with the c_dir */

    if (strlen(flag->c_dir) + strlen(request->path + 8) > PATH_MAX) {
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
      }
      return -1;
    }
    *cgi_request = 1; /* set flag indicating cgi execution */

    strcpy(uri_path, flag->c_dir);
    strcat(uri_path, request->path + strlen(CGI_PREFIX)); /* begin at second slash */

    /* server realpath for this request will be the cgi-directory */
    if (realpath(flag->c_dir, server_real_path) == NULL) {
      /*log error*/
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_NOT_FOUND;
        /*
         * Bad server configuration since this should have been checked at
         * startup
         */
      }
      return -1;
    }
    chp = uri_path;
    while ((*chp != '?') && (*chp != '\0'))
      chp++;
    /*truncate req.path after ?*/
    if (*chp == '?') {
      *chp = '\0';
      strncpy(request->querystring, ++chp,
          sizeof(request->querystring) - strlen("QUERY_STRING="));
    }
    /*check the uri contain ? or not*/
  } else if (flag->c_dir != NULL && strstr(request->path, "?") != NULL) {
    char *chp = request->path;
    while ((*chp != '?') && (*chp != '\0'))
      chp++;
    /*truncate req.path after ?*/
    if (*chp == '?') {
      *cgi_request = 1;
      *chp = '\0';
      if (strlen(flag->c_dir) + strlen(request->path) > PATH_MAX
          || strlen(chp + 1) > PATH_MAX) {
        if (uri_status != NULL)
          *uri_status = RESPONSE_STATUS_BAD_REQUEST;
        return -1;
      }
      strncpy(query_string, ++chp, PATH_MAX);
      strcpy(uri_path, flag->c_dir);
      strcat(uri_path, request->path);
      if (realpath(flag->dir, server_real_path) == NULL) {
        /*log error*/
        if (uri_status != NULL) {
          *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
          /*
           * Bad server configuration since this should
           * have been checked at startup
           */
        }
        return -1;
      }
    }

  } else {
    /* get the server "realpath" */
    if (realpath(flag->dir, server_real_path) == NULL) {
      /*log error*/
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
        /*
         * Bad server configuration since this should have been checked at
         * startup
         */
      }
      return -1;
    }

    /* append base/server directory to requested uri */
    if ((strlen(server_real_path) + strlen(request->path)) > PATH_MAX) {
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
      }
      return -1;
    }

    if ((strcpy(uri_path, server_real_path) == NULL)
        || (strcat(uri_path, request->path) == NULL)) {
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
      }
      return -1;
      /* log error */
    }
  }

  if (*cgi_request) {
    /* need to be able to read and execute the script */
    mode = R_OK | X_OK;
  } else {
    /* check access based on request method */
    switch (request->method) {
    case REQUEST_METHOD_GET:
    case REQUEST_METHOD_HEAD:
      mode = R_OK;
      break;
    case REQUEST_METHOD_POST:
      mode = R_OK | X_OK;
      break;
    default: /* PROBABLY WILL NEVER GET HERE */
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
      }
      break;
    }
  }

  if (access(uri_path, mode) != 0) {
    switch (errno) {
    case EACCES:
    case EROFS:
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_FORBIDDEN;
      }
      return -1;
      /* NOTREACHED */
      break;

    case ENAMETOOLONG:
    case ELOOP:
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
      }
      return -1;
      /* NOTREACHED*/
      break;

    case ENOENT:
    case ENOTDIR:
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_NOT_FOUND;
      }
      return -1;
      /* NOTREACHED*/
      break;

    default:
      if (uri_status != NULL) {
        *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
      }
      return -1;
    }
    return -1;
  }

  /*
   * At this point we know that the file exists and can be accessed by the
   * server.  Now we need to make sure that it is within the server's directory
   */

  /* eliminate /./ and /../ from the path */
  if (realpath(uri_path, uri_real_path) == NULL) {
    /*log error*/
    if (uri_status != NULL) {
      *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    }
    return -1;
  }

  if (strstr(uri_real_path, server_real_path) != NULL) {

    if (uri_status != NULL) {
      *uri_status = RESPONSE_STATUS_OK;
    }

    if (*cgi_request) {
      strcpy(realpath_str, (char*) uri_real_path);
    } else {
      /* check if uri_real_path points to a directory and if it does check for
       * index.html
       */
      check_index_html(uri_real_path, realpath_str);
    }
    return 0;
  } else {
    if (uri_status != NULL) {
      *uri_status = RESPONSE_STATUS_FORBIDDEN;
    }
    return -1;
  }

  /*NOTREACHED*/
  return -1;
}

/**
 * Checks if index.html exists.
 *
 * @param path the path where an index.html file is supposed to be located.
 * @param index_html stores the path to index.html in this variable.
 * @return 0, if successful and -1 otherwise.
 */
int
check_index_html(const char * path, char * index_html)
{
  /* check for index.html */
  struct stat st_stat_index;

  if (stat(path, &st_stat_index) == -1) {
    strcpy(index_html, path);
    return -1;
  }

  if (!S_ISDIR(st_stat_index.st_mode)) {
    strcpy(index_html, path);
    return 0;
  }

  if (strlen(path) + strlen(INDEX_HTML) < PATH_MAX) {

    /*char index_html[PATH_MAX+1];*/

    strcpy(index_html, path);
    if (index_html[strlen(index_html) - 1] == '/') {
      strcat(index_html, INDEX_HTML);
    } else {
      strcat(index_html, "/" INDEX_HTML);
    }

    if (stat(index_html, &st_stat_index) == 0) {
      /* at this point index.html exists */

      /* check that index.html is a file and not a directory */
      if (!S_ISDIR(st_stat_index.st_mode)) {
        if (access(index_html, R_OK) == 0) {
          return 0;
        } else {
          strcpy(index_html, path);
          return 0;
        }
      } else {
        /* it is a directory */
        strcpy(index_html, path);
        return 0;
      }
    } else {
      if (errno == ENOENT) {
        /* index.html does not exist in the directory */
      } else {
        perror("error stating " INDEX_HTML);
      }
    }
  }
  strcpy(index_html, path);
  return 0;
}

/**
 * Sends a generic error page to client and headers, if desired.
 *
 * @param response the response information.
 * @param simple_response 1 if no headers are to be sent. 0 otherwise.
 * @param socket the socket to which the client is connected.
 * @return 0 if successful. -1 otherwise.
 */
int
send_generic_page(struct response * response, int simple_response, int socket,
    char * custom_msg)
{
  char buf[BUF_SIZE];
  char message[256];
  size_t buf_size_remain;
  int written;
  char * buf_pos;
  int code;

  code = response->code;
  /* create custom message for status code */
  switch (code) {
  case RESPONSE_STATUS_OK:
    /* shouldn't be needed, but just in case */
    written = write_buffer(message, sizeof(message), "%d - OK", code);
    break;
  case RESPONSE_STATUS_BAD_REQUEST:
    written = write_buffer(message, sizeof(message), "%d - Bad Request", code);
    break;
  case RESPONSE_STATUS_FORBIDDEN:
    written = write_buffer(message, sizeof(message), "%d - Forbidden", code);
    break;
  case RESPONSE_STATUS_NOT_FOUND:
    written = write_buffer(message, sizeof(message), "%d - File Not Found",
        code);
    break;
  case RESPONSE_STATUS_NOT_IMPLEMENTED:
    written = write_buffer(message, sizeof(message),
        "%d - Method Not Implemented", code);
    break;
  case RESPONSE_STATUS_VERSION_NOT_SUPPORTED:
    written = write_buffer(message, sizeof(message),
        "%d - HTTP Version Not Supported", code);
    break;
  case RESPONSE_STATUS_CONNECTION_TIMED_OUT:
    return 0;
    /* NOTREACHED */
    written = write_buffer(message, sizeof(message), "%d - Bad Request", code);
    break;
  case RESPONSE_STATUS_INTERNAL_SERVER_ERROR:
    written = write_buffer(message, sizeof(message),
        "%d - Internal Server Error", code);
    break;
  default:
    written = write_buffer(message, sizeof(message), "%d - Unknown", code);
    break;
  }

  if (written < 0) {
    warnx("error writing to buffer\n");
  }

  /* begin html response */
  buf_size_remain = sizeof(buf);
  buf_pos = buf;

  written = write_buffer(buf_pos, buf_size_remain, "<html>%s<head>%s", CRLF,
  CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* write custom title */
  written = write_buffer(buf_pos, buf_size_remain,
      "<title>Team Geronimo - %s</title>%s</head>%s", message, CRLF, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* begin html body */
  written = write_buffer(buf_pos, buf_size_remain,
      "<body>%s<h1>Team Geronimo</h1>%s", CRLF, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* write custom body */
  if (custom_msg != NULL) {
    written = write_buffer(buf_pos, buf_size_remain,
        "<p>%s</p>%s<p>%s</p>%s</body>%s</html>%s", message, CRLF, custom_msg,
        CRLF, CRLF, CRLF);
  } else {
    written = write_buffer(buf_pos, buf_size_remain,
        "<p>%s</p>%s</body>%s</html>%s", message, CRLF, CRLF, CRLF);
  }

  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  response->content_length = buf_pos - buf;
  strncpy(response->content_type, "text/html",
      sizeof(response->content_type) - 1);

  if (coderesp(response, socket, !simple_response) != 0) {
    warnx("failed to send headers");
    return -1;
  }

  if (write(socket, buf, sizeof(buf) - buf_size_remain) < 0) {
    warn("write failed");
    return -1;
  }

  /*
   * Sample response entity body:
   *
   <html>
   <head>
   <title>Test page</title>
   </head>
   <body>
   <h1>Team Geronimo</h1>
   <p>This is our test page.</p>
   <img src="geronimo_iv.png"></img>
   </body>
   </html>
   */
  return 0;
}

/**
 * Creates HTML text containing a directory listing for the given
 * request path and sends it over the given socket.
 *
 * @param request the client request.
 * @param socket the client socket.
 * @return 0 on success and -1 otherwise.
 */
int
send_directory_listing(struct request * request, int socket)
{
  char buf[BUF_SIZE];
  size_t buf_size_remain;
  int written;
  char * buf_pos;
  struct dirent ** namelist;
  int entries;
  int i;

  buf_size_remain = sizeof(buf);
  buf_pos = buf;

  entries = scandir(request->path, &namelist, 0, alphasort);
  if (entries < 0) {
    perror("scandir");
  }

  /* begin html response */
  written = write_buffer(buf_pos, buf_size_remain, "<html>%s<head>%s", CRLF,
  CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* write custom title */
  written = write_buffer(buf_pos, buf_size_remain,
      "<title>Team Geronimo - %s</title>%s</head>%s", basename(request->path),
      CRLF, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* begin html body */
  written = write_buffer(buf_pos, buf_size_remain,
      "<body>%s<h1>Directory Listing for %s</h1>%s<p>%s", CRLF,
      basename(request->path), CRLF, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* write each entry */
  for (i = 0; i < entries; i++) {
    /* if buffer almost full write it out to socket */
    if (buf_size_remain < strlen(namelist[i]->d_name) + 2) { /* +2 for CRLF */
      if (write(socket, buf, sizeof(buf) - buf_size_remain) < 0) {
        perror("error writing directory listing");
        return -1;
      }
      buf_pos = buf;
      buf_size_remain = sizeof(buf);
    }

    /* write each entry in the directory in a separate line */
    if ((strlen(namelist[i]->d_name) >= 1) && (namelist[i]->d_name[0] != '.')) {
      written = write_buffer(buf_pos, buf_size_remain, "%s%s",
          namelist[i]->d_name, CRLF);
      if (written < 0) {
        warnx("failed to write to buffer\n");
        return -1;
      }
      buf_pos += written;
      buf_size_remain -= written;
    }
    free(namelist[i]);
  }
  free(namelist);

  /* write closing html headers */
  /* if buffer almost full write it out to socket */
  if (buf_size_remain < 100) {
    if (write(socket, buf, sizeof(buf) - buf_size_remain) < 0) {
      perror("error writing directory listing");
      return -1;
    }
    buf_pos = buf;
    buf_size_remain = sizeof(buf);
  }

  written = write_buffer(buf_pos, buf_size_remain, "</p>%s</body>%s</html>%s",
  CRLF, CRLF, CRLF);
  if (written < 0) {
    warnx("failed to write to buffer\n");
    return -1;
  }
  buf_pos += written;
  buf_size_remain -= written;

  /* write remaining contents of buf */
  if (write(socket, buf, sizeof(buf) - buf_size_remain) < 0) {
    perror("error writing directory listing");
    return -1;
  }

  return 0;
}

/**
 * Executes the given CGI script.
 * 
 * @param path is the path (uri) requested by the client
 *  need to make sure cgi_path can be accessed
 * @param uri_status, int * to be changed by the function
 *  after execution the value of uri_status will be the appropriate http
 *  response to the client 200 OK, 403 Forbidden, 404 Not Found, etc.
 *  It can be set to NULL if not required
 * @param flag user-provided flags.
 * @param cgi_path path where the cgi script is located.
 * @param socket server socket that is connected to a client.
 * @return 0 on success (equivalent to 200 OK). Otherwise -1.
 */
int
execute_cgi(struct request * request, struct flags * flag, int * uri_status,
    char * cgi_path, int socket)
{
  char meth_env[255];
  char query_env[255];
  char length_env[255];
  char query_string[255];
  char type_env[255];
  int cgi_output[2];
  int cgi_input[2];
  pid_t pid;
  int status;
  int i;
  char c;
  int content_length = request->content_length;

  if (request->method == REQUEST_METHOD_GET
      || request->method == REQUEST_METHOD_HEAD) {
    if (!request->querystring) {
      sprintf(query_env, "QUERY_STRING =%s", query_string);
    }
    if (request->method == REQUEST_METHOD_GET) {
      sprintf(meth_env, "REQUEST_METHOD =GET");
    } else {
      sprintf(meth_env, "REQUEST_METHOD =HEAD");
    }
  } else if (request->method == REQUEST_METHOD_POST) {
    if (content_length <= 0) {
      if (uri_status != NULL)
        *uri_status = RESPONSE_STATUS_BAD_REQUEST;
      return -1;
    }
    sprintf(meth_env, "REQUEST_METHOD =POST");
  } else {
    /*may not happen*/
    if (uri_status != NULL)
      *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    return -1;
  }
  sprintf(length_env, "CONTENT_LENGTH =%d", content_length);
  sprintf(type_env, "CONTENT_TYPE =%s", request->content_type);
  if (pipe(cgi_output) < 0) {
    if (uri_status != NULL)
      *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    return -1;
  }
  if (pipe(cgi_input) < 0) {
    if (uri_status != NULL)
      *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    return -1;
  }

  if ((pid = fork()) < 0) {
    if (uri_status != NULL)
      *uri_status = RESPONSE_STATUS_INTERNAL_SERVER_ERROR;
    return -1;
  }
  if (pid == 0) {/* child excute CGI */
    dup2(cgi_output[1], 1);
    dup2(cgi_input[0], 0);
    close(cgi_output[0]);
    close(cgi_input[1]);
    if (request->method == REQUEST_METHOD_GET
        || request->method == REQUEST_METHOD_HEAD) {
      putenv(query_env);
    }
    putenv(meth_env);
    putenv(length_env);
    putenv(type_env);
    execl(cgi_path, cgi_path, (char *) NULL);
    exit(0);
  } else { /* parent */
    close(cgi_output[1]);
    close(cgi_input[0]);

    if (request->method == REQUEST_METHOD_POST)
      for (i = 0; i < content_length; i++) {
        recv(socket, &c, 1, 0);
        if (write(cgi_input[1], &c, 1) < 0) {
          warn("write failed");
        }
      }

    while (read(cgi_output[0], &c, 1) > 0)
      send(socket, &c, 1, 0);

    close(cgi_output[0]);
    close(cgi_input[1]);

    /*TODO: can use signal*/
    waitpid(pid, &status, 0);
    if (uri_status != NULL) {
      *uri_status = RESPONSE_STATUS_OK;
    }
    return 0;
  }
}
