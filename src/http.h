/*
 * http.h - Header for http server
 * Copyright (c) 2013
 * Project Team Geronimo 
 * Stevens Institute of Technology.
 *
 * Implementation of a simple HTTP 1.0 web server named sws.
 */

#ifndef _SWS_HTTP_H_
#define _SWS_HTTP_H_

#include <time.h>
#include <limits.h>
#include <libgen.h>
#include <pwd.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/types.h>

#include "util.h"

#define BUF_SIZE (4 * 1024)
#define REQUEST_METHOD_GET  1
#define REQUEST_METHOD_HEAD 2
#define REQUEST_METHOD_POST 3




/**
 * The request structure contains the relevant portions of the http request
 * received by the client
 */
struct request
{
  char path[PATH_MAX + 1]; /* requested resource URI */
  int method; /* REQUEST_METHOD_? where ? is GET, HEAD or POST */
  time_t if_modified_since_date; /* If-Modified-Since field */
  int content_length; /*content_length field  for cgi request*/
  char content_type[64];/*content_type field for cgi request*/
  char querystring[255];/*for cgi GET*/
  /* only version 0.9 and 1.0 are valid */
  int version_major;
  int version_minor;
};

/**
 * Contains supported HTTP response codes.
 */
enum response_status_codes
{
  RESPONSE_STATUS_OK = 200,
  RESPONSE_STATUS_BAD_REQUEST = 400,
  RESPONSE_STATUS_FORBIDDEN = 403,
  RESPONSE_STATUS_NOT_FOUND = 404,
  RESPONSE_STATUS_NOT_IMPLEMENTED = 501,
  RESPONSE_STATUS_VERSION_NOT_SUPPORTED = 505,
  RESPONSE_STATUS_CONNECTION_TIMED_OUT = 522,
  RESPONSE_STATUS_INTERNAL_SERVER_ERROR = 500
};

/**
 * Structure for variable parameters in server response header.
 * All fields must be valid according to RFC 1945.
 */
struct response
{
  int code; /* Response code filed */
  time_t last_modified; /* Last-Modified field */
  char content_type[64]; /* Content-Type field */
  int content_length; /* Content-Length field */
};

void
init_response(struct response *, int);
int
httpd(int, struct flags *, const char *);
int
coderesp(struct response *, int, int);
int
fileserver(struct request *, struct response *, int, int, struct flags *);
int
checkuri(struct request *, int *, struct flags *, char *, int *);
int
check_index_html(const char * path, char * index_html);
int
send_generic_page(struct response *, int, int, char *);
int
send_directory_listing(struct request *, int);
int
execute_cgi(struct request * , struct flags * , int *, char * ,int);

#endif /* !_SWS_HTTP_H_ */
