/*
 * Copyright (c) 2013
 *   Fabian Foerg. Stevens Institute of Technology.
 *
 * Implementation of a simple HTTP 1.0 web server named sws.
 */

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "net.h"
#include "util.h"

#ifdef __linux__
#include <bsd/stdlib.h>
#include <bsd/string.h>
#endif

#define MIN_PORT 1
#define MAX_PORT 65535

int
main(int, char *[]);
static void
usage(void);

/*
 * Parses flags and starts the server.
 */
int
main(int argc, char *argv[])
{
  struct flags flag;
  int ch;

  flags_init(&flag);
  setprogname((char *) argv[0]);

  while ((ch = getopt(argc, argv, FLAGS_SUPPORTED)) != -1) {
    switch (ch) {
    case 'c':
      flag.c_dir = optarg;
      if (!is_dir(flag.c_dir)) {
        errx(EXIT_FAILURE, "invalid CGI dir");
      }
      break;
    case 'd':
      flag.dflag = 1;
      break;
    case 'h':
      usage();
      exit(EXIT_SUCCESS);
      /* NOTREACHED */
      break;
    case 'i':
      flag.i_address = optarg;
      {
        struct in_addr addr4;
        struct in6_addr addr6;
        if (inet_pton(AF_INET, flag.i_address, &addr4) == 1) {
          flag.ipv6 = 0;
        } else if (inet_pton(AF_INET6, flag.i_address, &addr6) == 1) {
          flag.ipv6 = 1;
        } else {
          errx(EXIT_FAILURE, "neither valid IPv4 nor IPv6 address %s",
              flag.i_address);
        }
      }
      break;
    case 'l':
      flag.lflag=1;
      flag.l_log_file = optarg;
      if((flag.logfd=open(flag.l_log_file,O_CREAT | O_APPEND | O_WRONLY,0666))<0){
        perror("Logfile error");
        exit(EXIT_FAILURE);
      }
      break;
    case 'p':
      flag.p_port = atoi(optarg);
      if ((flag.p_port < MIN_PORT) || (flag.p_port > MAX_PORT)) {
        errx(EXIT_FAILURE, "port must be between %d and %d", MIN_PORT,
        MAX_PORT);
      }
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
      /* NOTREACHED */
      break;
    }
  }
  argc -= optind;
  argv += optind;

  /* get mandatory dir parameter */
  if (argc != 1) {
    usage();
    exit(EXIT_FAILURE);
  }
  flag.dir = argv[0];

  if (!is_dir(flag.dir)) {
    errx(EXIT_FAILURE, "invalid dir");
  }

#if DEBUG
  /* enable debugging flag */
  flag.dflag = 1;
#endif

  run_server(&flag);
  close(flag.logfd);
  return EXIT_SUCCESS;
}

/*
 * Prints usage information and terminates this process.
 */
static void
usage(void)
{
  (void) fprintf(stderr,
      "usage: %s [-dh] [-c dir] [-i address] [-l file] [-p port] dir\n",
      getprogname());
}

