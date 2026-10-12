/**
 * @file uvbus_internal.h
 * @brief Hooks that sit between UVBus and the layer above it.
 *
 * Everything here is for use inside the library. It is a separate header so
 * that uvbus.h -- which users include -- stays free of mechanisms that exist
 * only to serve uvrpc_server.c.
 */

#ifndef UVBUS_INTERNAL_H
#define UVBUS_INTERNAL_H

#include "uvbus.h"

/**
 * @brief Report the loss of an accepted connection.
 *
 * Fired by a server transport when one of the connections it accepted goes
 * away -- the peer closed, or the read failed -- before that connection's
 * block is released. `client_ctx` is the same pointer the transport passes as
 * the `client_ctx` argument of its recv callback, so the layer above can
 * recognise which of the connections it was tracking has ended.
 *
 * It carries identity on purpose. uvbus_close_callback_t is
 * `void (*)(void*)`, which can say "something closed" but not what, and the
 * server tracks every connection it has seen by pointer; a close without a
 * name leaves that table growing forever, which is how a server with
 * max_clients = 1 came to accept one client for the rest of its life.
 *
 * Transports that have no connections to lose -- UDP, which is datagram-based
 * and identifies senders by address rather than by connection -- simply never
 * call it.
 *
 * @param bus Server bus, as returned by uvbus_server_new().
 * @param handler Called per lost connection, or NULL to stop reporting.
 * @param server_ctx Passed through to the handler as its second argument.
 */
void uvbus_set_conn_close_handler(uvbus_t* bus,
                                  void (*handler)(void* client_ctx, void* server_ctx),
                                  void* server_ctx);

#endif /* UVBUS_INTERNAL_H */
