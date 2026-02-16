/**
 * UVBus Configuration Constants
 */

#ifndef UVBUS_CONFIG_H
#define UVBUS_CONFIG_H

/* Buffer sizes */
#define UVBUS_MAX_BUFFER_SIZE 65536
#define UVBUS_DEFAULT_BUFFER_SIZE 4096

/* Client management */
#define UVBUS_INITIAL_CLIENT_CAPACITY 10
#define UVBUS_MAX_CLIENTS 1024

/* Server settings */
#define UVBUS_BACKLOG 128
#define UVBUS_MAX_ENDPOINTS 256

/* Hash table size */
#define UVBUS_HASH_TABLE_SIZE 256

/* Timeout settings */
#define UVBUS_DEFAULT_TIMEOUT_MS 5000
#define UVBUS_MIN_TIMEOUT_MS 100
#define UVBUS_MAX_TIMEOUT_MS 60000

#endif /* UVBUS_CONFIG_H */