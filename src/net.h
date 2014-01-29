/*
 * net.h
 *
 * Network functionality for server.
 */

#ifndef _SWS_NET_H_
#define _SWS_NET_H_

#include "util.h"

#define DEFAULT_PORT 8080

void
run_server(struct flags*);
void
wait_for_data(int);

#endif /* !_SWS_NET_H_ */
