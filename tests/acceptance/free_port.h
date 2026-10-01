/*
 * Ask the OS for a TCP port instead of hard-coding one.
 *
 * Hard-coded ports make a test that fails for reasons unrelated to what it
 * tests: a previous run leaves the port in TIME_WAIT, or something else on the
 * machine already has it, or two runs overlap. Observed here -- one run in five
 * under ASan failed and then passed four times running.
 *
 * Binding to port 0 and asking the kernel which port it picked avoids all of
 * that. There is still a window between closing the probe socket and the server
 * binding, so a caller that hits it should retry; acceptance_free_port_retry()
 * does that and reports a port that a uvrpc server actually managed to bind.
 *
 * Static header because six scenarios need it and none of them should own a
 * copy of the trick.
 */

#ifndef UVRPC_ACCEPTANCE_FREE_PORT_H
#define UVRPC_ACCEPTANCE_FREE_PORT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* A port nothing is listening on, as far as the kernel can tell right now. */
static int acceptance_free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* let the kernel choose */

    int bound = bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    socklen_t len = sizeof(addr);
    int port = -1;
    if (bound && getsockname(fd, (struct sockaddr*)&addr, &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    close(fd);

    return port;
}

/* A port a uvrpc server can actually bind, given a way to try one. `try_port`
 * returns non-zero when the server came up on that port. */
static int acceptance_free_port_retry(int (*try_port)(int port)) {
    for (int attempt = 0; attempt < 20; attempt++) {
        int port = acceptance_free_port();
        if (port > 0 && try_port(port)) {
            return port;
        }
    }
    return -1;
}

#endif /* UVRPC_ACCEPTANCE_FREE_PORT_H */
