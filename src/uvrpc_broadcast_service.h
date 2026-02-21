/**
 * UVRPC Broadcast Service API Generator
 * Automatically generated from rpc_broadcast.fbs schema
 *
 * This file demonstrates DSL-driven code generation for broadcast services.
 * Each method in the BroadcastService gets type-safe publisher/subscriber APIs.
 */

#ifndef UVRPC_BROADCAST_SERVICE_H
#define UVRPC_BROADCAST_SERVICE_H

#include "../include/uvrpc.h"
#include "rpc_broadcast_reader.h"
#include "rpc_broadcast_builder.h"
#include "uvrpc_broadcast.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
   Generated Broadcast Service: BroadcastService
   ============================================================ */

/* Method: PublishNews (NewsPublishRequest -> NewsPublishResponse) */

/**
 * Publish news to all subscribers
 * @param publisher UVRPC publisher instance
 * @param title News title
 * @param content News content
 * @param timestamp Publication timestamp
 * @param author News author
 * @param callback Completion callback
 * @param ctx User context
 * @return UVRPC_OK on success, error code otherwise
 */
static inline int uvrpc_broadcast_service_publish_news(
    uvrpc_publisher_t* publisher,
    const char* title,
    const char* content,
    int64_t timestamp,
    const char* author,
    uvrpc_publish_callback_t callback,
    void* ctx) {

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Create NewsPublishRequest */
    rpc_NewsPublishRequest_start_as_root(&builder);
    rpc_NewsPublishRequest_title_add(&builder, flatbuffers_string_create_str(&builder, title));
    rpc_NewsPublishRequest_content_add(&builder, flatbuffers_string_create_str(&builder, content));
    rpc_NewsPublishRequest_timestamp_add(&builder, timestamp);
    rpc_NewsPublishRequest_author_add(&builder, flatbuffers_string_create_str(&builder, author));
    rpc_NewsPublishRequest_end_as_root(&builder);

    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);

    /* Publish using broadcast message format */
    uint8_t* broadcast_msg = NULL;
    size_t broadcast_size = 0;
    int ret = uvrpc_broadcast_encode("PublishNews", (const uint8_t*)buf, size, &broadcast_msg, &broadcast_size);

    if (ret == UVRPC_OK && broadcast_msg) {
        ret = uvrpc_publisher_publish(publisher, "PublishNews", broadcast_msg, broadcast_size, callback, ctx);
        uvrpc_free(broadcast_msg);
    }

    flatcc_builder_clear(&builder);

    return ret;
}

/**
 * Decode received PublishNews message
 * @param data Message data
 * @param size Message size
 * @param out_title Output title (caller must free)
 * @param out_content Output content (caller must free)
 * @param out_timestamp Output timestamp
 * @param out_author Output author (caller must free)
 * @return UVRPC_OK on success
 */
static inline int uvrpc_broadcast_service_decode_publish_news(
    const uint8_t* data,
    size_t size,
    char** out_title,
    char** out_content,
    int64_t* out_timestamp,
    char** out_author) {

    if (!data || !out_title || !out_content || !out_timestamp || !out_author) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    rpc_NewsPublishRequest_table_t request = rpc_NewsPublishRequest_as_root(data);
    if (!request) {
        return UVRPC_ERROR;
    }

    *out_title = uvrpc_strdup(rpc_NewsPublishRequest_title(request));
    *out_content = uvrpc_strdup(rpc_NewsPublishRequest_content(request));
    *out_timestamp = rpc_NewsPublishRequest_timestamp(request);
    *out_author = uvrpc_strdup(rpc_NewsPublishRequest_author(request));

    return UVRPC_OK;
}

/* Method: UpdateWeather (WeatherUpdateRequest -> WeatherUpdateResponse) */

/**
 * Publish weather update to all subscribers
 * @param publisher UVRPC publisher instance
 * @param location Weather location
 * @param temperature Temperature value
 * @param humidity Humidity percentage
 * @param condition Weather condition
 * @param timestamp Update timestamp
 * @param callback Completion callback
 * @param ctx User context
 * @return UVRPC_OK on success
 */
static inline int uvrpc_broadcast_service_update_weather(
    uvrpc_publisher_t* publisher,
    const char* location,
    float temperature,
    int32_t humidity,
    const char* condition,
    int64_t timestamp,
    uvrpc_publish_callback_t callback,
    void* ctx) {

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Create WeatherUpdateRequest */
    rpc_WeatherUpdateRequest_start_as_root(&builder);
    rpc_WeatherUpdateRequest_location_add(&builder, flatbuffers_string_create_str(&builder, location));
    rpc_WeatherUpdateRequest_temperature_add(&builder, temperature);
    rpc_WeatherUpdateRequest_humidity_add(&builder, humidity);
    rpc_WeatherUpdateRequest_condition_add(&builder, flatbuffers_string_create_str(&builder, condition));
    rpc_WeatherUpdateRequest_timestamp_add(&builder, timestamp);
    rpc_WeatherUpdateRequest_end_as_root(&builder);

    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);

    /* Publish using broadcast message format */
    uint8_t* broadcast_msg = NULL;
    size_t broadcast_size = 0;
    int ret = uvrpc_broadcast_encode("UpdateWeather", (const uint8_t*)buf, size, &broadcast_msg, &broadcast_size);

    if (ret == UVRPC_OK && broadcast_msg) {
        ret = uvrpc_publisher_publish(publisher, "UpdateWeather", broadcast_msg, broadcast_size, callback, ctx);
        uvrpc_free(broadcast_msg);
    }

    flatcc_builder_clear(&builder);

    return ret;
}

/**
 * Decode received UpdateWeather message
 */
static inline int uvrpc_broadcast_service_decode_update_weather(
    const uint8_t* data,
    size_t size,
    char** out_location,
    float* out_temperature,
    int32_t* out_humidity,
    char** out_condition,
    int64_t* out_timestamp) {

    if (!data || !out_location || !out_temperature || !out_humidity || !out_condition || !out_timestamp) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    rpc_WeatherUpdateRequest_table_t request = rpc_WeatherUpdateRequest_as_root(data);
    if (!request) {
        return UVRPC_ERROR;
    }

    const char* loc = rpc_WeatherUpdateRequest_location(request);
    const char* cond = rpc_WeatherUpdateRequest_condition(request);

    *out_location = loc ? uvrpc_strdup(loc) : NULL;
    *out_temperature = rpc_WeatherUpdateRequest_temperature(request);
    *out_humidity = rpc_WeatherUpdateRequest_humidity(request);
    *out_condition = cond ? uvrpc_strdup(cond) : NULL;
    *out_timestamp = rpc_WeatherUpdateRequest_timestamp(request);

    return UVRPC_OK;
}

/* Method: NotifyEvent (EventNotificationRequest -> EventNotificationResponse) */

/**
 * Publish event notification to all subscribers
 */
static inline int uvrpc_broadcast_service_notify_event(
    uvrpc_publisher_t* publisher,
    const char* event_type,
    const uint8_t* event_data,
    size_t event_data_size,
    int64_t timestamp,
    int32_t priority,
    uvrpc_publish_callback_t callback,
    void* ctx) {

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    /* Create event data vector */
    flatbuffers_uint8_vec_ref_t data_ref = 0;
    if (event_data && event_data_size > 0) {
        data_ref = flatbuffers_uint8_vec_create(&builder, event_data, event_data_size);
    }

    /* Create EventNotificationRequest */
    rpc_EventNotificationRequest_start_as_root(&builder);
    rpc_EventNotificationRequest_event_type_add(&builder, flatbuffers_string_create_str(&builder, event_type));
    rpc_EventNotificationRequest_event_data_add(&builder, data_ref);
    rpc_EventNotificationRequest_timestamp_add(&builder, timestamp);
    rpc_EventNotificationRequest_priority_add(&builder, priority);
    rpc_EventNotificationRequest_end_as_root(&builder);

    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);

    /* Publish using broadcast message format */
    uint8_t* broadcast_msg = NULL;
    size_t broadcast_size = 0;
    int ret = uvrpc_broadcast_encode("NotifyEvent", (const uint8_t*)buf, size, &broadcast_msg, &broadcast_size);

    if (ret == UVRPC_OK && broadcast_msg) {
        ret = uvrpc_publisher_publish(publisher, "NotifyEvent", broadcast_msg, broadcast_size, callback, ctx);
        uvrpc_free(broadcast_msg);
    }

    flatcc_builder_clear(&builder);

    return ret;
}

/**
 * Decode received NotifyEvent message
 */
static inline int uvrpc_broadcast_service_decode_notify_event(
    const uint8_t* data,
    size_t size,
    char** out_event_type,
    const uint8_t** out_event_data,
    size_t* out_event_data_size,
    int64_t* out_timestamp,
    int32_t* out_priority) {

    if (!data || !out_event_type || !out_event_data || !out_event_data_size || !out_timestamp || !out_priority) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    rpc_EventNotificationRequest_table_t request = rpc_EventNotificationRequest_as_root(data);
    if (!request) {
        return UVRPC_ERROR;
    }

    const char* ev_type = rpc_EventNotificationRequest_event_type(request);
    *out_event_type = ev_type ? uvrpc_strdup(ev_type) : NULL;

    flatbuffers_uint8_vec_t ev_data = rpc_EventNotificationRequest_event_data(request);
    if (ev_data) {
        *out_event_data = ev_data;
        *out_event_data_size = flatbuffers_uint8_vec_len(ev_data);
    } else {
        *out_event_data = NULL;
        *out_event_data_size = 0;
    }

    *out_timestamp = rpc_EventNotificationRequest_timestamp(request);
    *out_priority = rpc_EventNotificationRequest_priority(request);

    return UVRPC_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_BROADCAST_SERVICE_H */