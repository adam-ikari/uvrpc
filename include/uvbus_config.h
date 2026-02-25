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
#define UVBUS_MAX_BUFFER_SIZE 1048576    /**< @brief Maximum buffer size (1MB for UDP) */
#define UVBUS_DEFAULT_BUFFER_SIZE 65536  /**< @brief Default buffer size (64KB) */
/** @} */

/** @defgroup FrameSettings Frame Settings */
/** @{ */
#define UVBUS_MAX_FRAME_SIZE 1048576     /**< @brief Maximum frame size (1MB) */
#define UVBUS_DEFAULT_MAX_FRAME_SIZE 65536 /**< @brief Default maximum frame size (64KB) for stability */
/** @} */

/** @defgroup ClientSettings Client Settings */
/** @{ */
#define UVBUS_INITIAL_CLIENT_CAPACITY 10 /**< @brief Initial client capacity */
#define UVBUS_MAX_CLIENTS 1024           /**< @brief Maximum number of clients */
/** @} */

/** @defgroup ServerSettings Server Settings */
/** @{ */
#define UVBUS_BACKLOG 1024               /**< @brief Server backlog (increased for high concurrency) */
/** @} */

/** @defgroup HashTableSettings Hash Table Settings */
/** @{ */
#define UVBUS_HASH_TABLE_SIZE 256        /**< @brief Hash table size */
/** @} */

/** @defgroup OptimizationSettings Optimization Settings */
/** @{ */
/* Compile-time optimization options (currently empty) */
/** @} */

/** @defgroup TimeoutSettings Timeout Settings */
/** @{ */
#define UVBUS_DEFAULT_TIMEOUT_MS 5000    /**< @brief Default timeout in milliseconds */
#define UVBUS_MIN_TIMEOUT_MS 100         /**< @brief Minimum timeout in milliseconds */
#define UVBUS_MAX_TIMEOUT_MS 60000       /**< @brief Maximum timeout in milliseconds */
/** @} */

#ifdef __cplusplus
extern "C" {
#endif

#endif /* UVBUS_CONFIG_H */