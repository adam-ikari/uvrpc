/**
 * @file uvbus_config.h
 * @brief UVBus Configuration Constants
 * 
 * Defines default values and limits for UVBus configuration.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 */

#ifndef UVBUS_CONFIG_H
#define UVBUS_CONFIG_H

/** @defgroup BufferSettings Buffer Settings */
/** @{ */
#define UVBUS_MAX_BUFFER_SIZE 65536      /**< @brief Maximum buffer size */
#define UVBUS_DEFAULT_BUFFER_SIZE 4096   /**< @brief Default buffer size */
/** @} */

/** @defgroup ClientSettings Client Settings */
/** @{ */
#define UVBUS_INITIAL_CLIENT_CAPACITY 10 /**< @brief Initial client capacity */
#define UVBUS_MAX_CLIENTS 1024           /**< @brief Maximum number of clients */
/** @} */

/** @defgroup ServerSettings Server Settings */
/** @{ */
#define UVBUS_BACKLOG 128                /**< @brief Server backlog */
#define UVBUS_MAX_ENDPOINTS 256          /**< @brief Maximum number of endpoints */
/** @} */

/** @defgroup HashTableSettings Hash Table Settings */
/** @{ */
#define UVBUS_HASH_TABLE_SIZE 256        /**< @brief Hash table size */
/** @} */

/** @defgroup TimeoutSettings Timeout Settings */
/** @{ */
#define UVBUS_DEFAULT_TIMEOUT_MS 5000    /**< @brief Default timeout in milliseconds */
#define UVBUS_MIN_TIMEOUT_MS 100         /**< @brief Minimum timeout in milliseconds */
#define UVBUS_MAX_TIMEOUT_MS 60000       /**< @brief Maximum timeout in milliseconds */
/** @} */

#endif /* UVBUS_CONFIG_H */