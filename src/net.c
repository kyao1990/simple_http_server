/*
 * Network functionality for sws.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <assert.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef sun
#include <strings.h>
#endif

#include "http.h"
#include "net.h"
#include "util.h"

#define BACKLOG 5
#define CLIENT_TIMEOUT_SEC 20
#define UNKNOWN_IP "X.X.X.X"

static void
accept_client(struct flags *, int);
static void
handle_client(int, struct sockaddr_storage *, socklen_t, struct flags *);
static int
setup_server_socket(struct flags *);

/**
 * Waits for the client to send a request. Times out, if client does not
 * respond for some time.
 *
 * @param client_socket the client socket
 */
void
wait_for_data(int client_socket)
{
  fd_set rfds;
  struct timeval tv;
  int retval;

  FD_ZERO(&rfds);
  FD_SET(client_socket, &rfds);

  /* set timeout. */
  tv.tv_sec = CLIENT_TIMEOUT_SEC;
  tv.tv_usec = 0;

  retval = select(client_socket + 1, &rfds, NULL, NULL, &tv);

  if (retval < 0) {
    perror("select");
  } else if (retval == 0) {
    /* timeout occurred */
    struct response response;

    init_response(&response, RESPONSE_STATUS_CONNECTION_TIMED_OUT);
    coderesp(&response, client_socket, 1);
  }
}

/**
 * Determines the client's IP address, waits for a request,
 * and handles it.
 *
 * @param client_sock the client socket
 * @param client client socket information
 * @param client_length length of client structure
 * @param flag user-provided flags
 */
static void
handle_client(int client_sock, struct sockaddr_storage * client,
    socklen_t client_length, struct flags * flag)
{
  char client_ip[INET6_ADDRSTRLEN];
  int determined_client_addr;

  /* determine client IP address */
  determined_client_addr = 0;
  /* client IP is empty by default */
  bzero(client_ip, sizeof(client_ip));
  if (client->ss_family == AF_INET6) {
    if (inet_ntop(AF_INET6, &((struct sockaddr_in6*) client)->sin6_addr,
        client_ip, sizeof(client_ip)) == NULL) {
      perror("inet_ntop for client address");
    } else {
      determined_client_addr = 1;
    }
  } else if (client->ss_family == AF_INET) {
    if (inet_ntop(AF_INET, &((struct sockaddr_in *) client)->sin_addr.s_addr,
        client_ip, sizeof(client_ip)) == NULL) {
      perror("inet_ntop for client address");
    } else {
      determined_client_addr = 1;
    }
  }
  if (!determined_client_addr) {
    strncpy(client_ip, UNKNOWN_IP, sizeof(client_ip) - 1);
  }
  /* child process handles client and exits when done */
  if (httpd(client_sock, flag, client_ip) < 0) {
    if (determined_client_addr) {
      warnx("httpd failed for client %s", client_ip);
    } else {
      warnx("httpd failed for client with unknown address");
    }
  }
  (void) close(client_sock);
}

/**
 * Waits for client request. When a client connects a new process is forked
 * to handle the request. Calls function to communicate with it.
 *
 * @param flag user-provided flags
 * @param server_sock the server socket file descriptor
 */
static void
accept_client(struct flags *flag, int server_sock)
{
  struct sockaddr_storage client;
  socklen_t client_length;
  int client_sock;

  client_length = sizeof(client);
  client_sock = accept(server_sock, (struct sockaddr *) &client,
      &client_length);
  if (client_sock < 0) {
    perror("accept");
  } else {
    switch (fork()) {
    case -1:
      err(EXIT_FAILURE, "cannot fork child to handle client");
      /* NOTREACHED */
      /* existing childs become adopted by init. no need to reap them. */
      break;
    case 0:
      handle_client(client_sock, &client, client_length, flag);
      exit(EXIT_SUCCESS);
      /* NOTREACHED */
      break;
    default:
      /* parent cleans duplicate client socket */
      (void) close(client_sock);
      break;
    }
  }
}

/**
 * Binds server socket to specified addresses and port.
 *
 * @param flag user-provided flags.
 * @return the created server socket.
 */
static int
setup_server_socket(struct flags * flag)
{
  int server_sock;
  socklen_t server_length;
  struct sockaddr *server;
  struct sockaddr_in server4;
  struct sockaddr_in6 server6;
  int ipv6;
  int hport;

  /* use IPv6 if no address specified or IPv6 address specified */
  ipv6 = ((flag->i_address == NULL) || flag->ipv6);

  /* create socket */
  if (ipv6) {
    server_sock = socket(AF_INET6, SOCK_STREAM, 0);
  } else {
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
  }
  if (server_sock < 0) {
    err(EXIT_FAILURE, "opening stream socket");
  }

  /* server settings */
  /* set address */
  if (ipv6) {
    server6.sin6_family = AF_INET6;
    if (flag->i_address == NULL) {
      server6.sin6_addr = in6addr_any;
    } else {
      inet_pton(server6.sin6_family, flag->i_address, &(server6.sin6_addr));
    }
    server6.sin6_scope_id = 0;
    server6.sin6_flowinfo = 0;
    server6.sin6_port = ntohs(flag->p_port);
  } else {
    server4.sin_family = AF_INET;
    /* flag->i_address != NULL holds */
    inet_pton(server4.sin_family, flag->i_address, &(server4.sin_addr));
  }
  /* set port */
  hport = ntohs(flag->p_port);
  if (ipv6) {
    server6.sin6_port = hport;
  } else {
    server4.sin_port = hport;
  }

  /* bind socket */
  server_length = ipv6 ? sizeof(server6) : sizeof(server4);
  server = ipv6 ? (struct sockaddr *) &server6 : (struct sockaddr *) &server4;
  if (bind(server_sock, server, server_length) < 0) {
    err(EXIT_FAILURE, "binding stream socket");
  }
  if (getsockname(server_sock, server, &server_length) < 0) {
    err(EXIT_FAILURE, "getting socket name");
  }

  return server_sock;
}

/**
 * Starts the server and transits into daemon mode, if not in debug mode.
 * Loops forever, accepting stream (TCP)  connections.
 * Child is forked when a client connects.
 *
 * @param flag user-provided flags.
 */
void
run_server(struct flags* flag)
{
  int server_sock;

  /* start listening for clients */
  server_sock = setup_server_socket(flag);

  /* attach signal handlers */
  if (signal(SIGCHLD, server_sig_handler) == SIG_ERR) {
    err(EXIT_FAILURE, "cannot catch SIGCHLD");
  }
  if (signal(SIGHUP, server_sig_handler) == SIG_ERR) {
    err(EXIT_FAILURE, "cannot catch SIGCHUP");
  }

  /* Start accepting connections */
  listen(server_sock, BACKLOG);

  /* daemonize if not in debug mode */
  if (!flag->dflag) {
    if (daemon(1, 1) < 0) {
      errx(EXIT_FAILURE, "cannot transit into daemon mode");
    }
  }

  /* Handle clients */
  do {
    accept_client(flag, server_sock);
  } while (1);

  close(server_sock);
}
