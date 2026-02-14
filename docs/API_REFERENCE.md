# UVRPC API Reference

## 配置 API

### uvrpc_config_t

配置结构体，用于创建服务端和客户端。

```c
typedef struct uvrpc_config uvrpc_config_t;
```

#### uvrpc_config_new()

创建新的配置对象。

```c
uvrpc_config_t* uvrpc_config_new(void);
```

**返回值**：新配置对象指针，失败返回 NULL

#### uvrpc_config_free()

释放配置对象。

```c
void uvrpc_config_free(uvrpc_config_t* config);
```

**参数**：
- `config`：配置对象指针

#### uvrpc_config_set_loop()

设置事件循环。

```c
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
```

**参数**：
- `config`：配置对象指针
- `loop`：libuv 事件循环指针

**返回值**：配置对象指针

#### uvrpc_config_set_address()

设置地址。

```c
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);
```

**参数**：
- `config`：配置对象指针
- `address`：地址字符串（如 "tcp://127.0.0.1:5555"）

**返回值**：配置对象指针

#### uvrpc_config_set_transport()

设置传输类型。

```c
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_type transport);
```

**参数**：
- `config`：配置对象指针
- `transport`：传输类型（UVRPC_TRANSPORT_TCP/UDP/IPC/INPROC）

**返回值**：配置对象指针

#### uvrpc_config_set_comm_type()

设置通信类型。

```c
uvrpc_config_t* uvrpc_config_set_comm_type(uvrpc_config_t* config, uvrpc_comm_type_t comm_type);
```

**参数**：
- `config`：配置对象指针
- `comm_type`：通信类型（UVRPC_COMM_SERVER_CLIENT/BROADCAST）

**返回值**：配置对象指针

## 服务端 API

### uvrpc_server_t

服务端结构体。

```c
typedef struct uvrpc_server uvrpc_server_t;
```

#### uvrpc_server_create()

创建服务端对象。

```c
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
```

**参数**：
- `config`：配置对象指针

**返回值**：服务端对象指针，失败返回 NULL

#### uvrpc_server_start()

启动服务端。

```c
int uvrpc_server_start(uvrpc_server_t* server);
```

**参数**：
- `server`：服务端对象指针

**返回值**：UVRPC_OK 成功，其他值表示失败

#### uvrpc_server_stop()

停止服务端。

```c
void uvrpc_server_stop(uvrpc_server_t* server);
```

**参数**：
- `server`：服务端对象指针

#### uvrpc_server_free()

释放服务端对象。

```c
void uvrpc_server_free(uvrpc_server_t* server);
```

**参数**：
- `server`：服务端对象指针

#### uvrpc_server_register()

注册服务处理器。

```c
int uvrpc_server_register(uvrpc_server_t* server, const char* method, uvrpc_handler_t handler, void* ctx);
```

**参数**：
- `server`：服务端对象指针
- `method`：方法名称
- `handler`：处理函数
- `ctx`：用户上下文

**返回值**：UVRPC_OK 成功，其他值表示失败

## 客户端 API

### uvrpc_client_t

客户端结构体。

```c
typedef struct uvrpc_client uvrpc_client_t;
```

#### uvrpc_client_create()

创建客户端对象。

```c
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);
```

**参数**：
- `config`：配置对象指针

**返回值**：客户端对象指针，失败返回 NULL

#### uvrpc_client_connect()

连接到服务端。

```c
int uvrpc_client_connect(uvrpc_client_t* client);
```

**参数**：
- `client`：客户端对象指针

**返回值**：UVRPC_OK 成功，其他值表示失败

#### uvrpc_client_disconnect()

断开连接。

```c
void uvrpc_client_disconnect(uvrpc_client_t* client);
```

**参数**：
- `client`：客户端对象指针

#### uvrpc_client_free()

释放客户端对象。

```c
void uvrpc_client_free(uvrpc_client_t* client);
```

**参数**：
- `client`：客户端对象指针

#### uvrpc_client_call()

调用远程方法。

```c
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                       const uint8_t* params, size_t params_size,
                       uvrpc_callback_t callback, void* ctx);
```

**参数**：
- `client`：客户端对象指针
- `method`：方法名称
- `params`：参数数据
- `params_size`：参数大小
- `callback`：响应回调函数
- `ctx`：用户上下文

**返回值**：UVRPC_OK 成功，其他值表示失败

## 发布/订阅 API

### uvrpc_publisher_t

发布者结构体。

```c
typedef struct uvrpc_publisher uvrpc_publisher_t;
```

#### uvrpc_publisher_create()

创建发布者对象。

```c
uvrpc_publisher_t* uvrpc_publisher_create(uvrpc_config_t* config);
```

**参数**：
- `config`：配置对象指针

**返回值**：发布者对象指针，失败返回 NULL

#### uvrpc_publisher_start()

启动发布者。

```c
int uvrpc_publisher_start(uvrpc_publisher_t* publisher);
```

**参数**：
- `publisher`：发布者对象指针

**返回值**：UVRPC_OK 成功，其他值表示失败

#### uvrpc_publisher_stop()

停止发布者。

```c
void uvrpc_publisher_stop(uvrpc_publisher_t* publisher);
```

**参数**：
- `publisher`：发布者对象指针

#### uvrpc_publisher_free()

释放发布者对象。

```c
void uvrpc_publisher_free(uvrpc_publisher_t* publisher);
```

**参数**：
- `publisher`：发布者对象指针

#### uvrpc_publisher_publish()

发布消息。

```c
int uvrpc_publisher_publish(uvrpc_publisher_t* publisher, const char* topic,
                             const uint8_t* data, size_t size,
                             uvrpc_publish_callback_t callback, void* ctx);
```

**参数**：
- `publisher`：发布者对象指针
- `topic`：主题
- `data`：消息数据
- `size`：消息大小
- `callback`：发布回调函数
- `ctx`：用户上下文

**返回值**：UVRPC_OK 成功，其他值表示失败

### uvrpc_subscriber_t

订阅者结构体。

```c
typedef struct uvrpc_subscriber uvrpc_subscriber_t;
```

#### uvrpc_subscriber_create()

创建订阅者对象。

```c
uvrpc_subscriber_t* uvrpc_subscriber_create(uvrpc_config_t* config);
```

**参数**：
- `config`：配置对象指针

**返回值**：订阅者对象指针，失败返回 NULL

#### uvrpc_subscriber_connect()

连接到发布者。

```c
int uvrpc_subscriber_connect(uvrpc_subscriber_t* subscriber);
```

**参数**：
- `subscriber`：订阅者对象指针

**返回值**：UVRPC_OK 成功，其他值表示失败

#### uvrpc_subscriber_disconnect()

断开连接。

```c
void uvrpc_subscriber_disconnect(uvrpc_subscriber_t* subscriber);
```

**参数**：
- `subscriber`：订阅者对象指针

#### uvrpc_subscriber_free()

释放订阅者对象。

```c
void uvrpc_subscriber_free(uvrpc_subscriber_t* subscriber);
```

**参数**：
- `subscriber`：订阅者对象指针

#### uvrpc_subscriber_subscribe()

订阅主题。

```c
int uvrpc_subscriber_subscribe(uvrpc_subscriber_t* subscriber, const char* topic,
                                 uvrpc_subscribe_callback_t callback, void* ctx);
```

**参数**：
- `subscriber`：订阅者对象指针
- `topic`：主题
- `callback`：消息回调函数
- `ctx`：用户上下文

**返回值**：UVRPC_OK 成功，其他值表示失败

#### uvrpc_subscriber_unsubscribe()

取消订阅主题。

```c
int uvrpc_subscriber_unsubscribe(uvrpc_subscriber_t* subscriber, const char* topic);
```

**参数**：
- `subscriber`：订阅者对象指针
- `topic`：主题

**返回值**：UVRPC_OK 成功，其他值表示失败

## 请求/响应 API

### uvrpc_request_t

请求结构体。

```c
typedef struct uvrpc_request uvrpc_request_t;
```

**字段**：
- `server`：服务端对象指针
- `msgid`：消息 ID
- `method`：方法名称
- `params`：参数数据
- `params_size`：参数大小
- `user_data`：用户数据

#### uvrpc_request_send_response()

发送响应。

```c
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                  const uint8_t* result, size_t result_size);
```

**参数**：
- `req`：请求对象指针
- `status`：状态码（UVRPC_OK 或错误码）
- `result`：结果数据
- `result_size`：结果大小

#### uvrpc_request_free()

释放请求对象。

```c
void uvrpc_request_free(uvrpc_request_t* req);
```

**参数**：
- `req`：请求对象指针

### uvrpc_response_t

响应结构体。

```c
typedef struct uvrpc_response uvrpc_response_t;
```

**字段**：
- `status`：状态码
- `msgid`：消息 ID
- `error_code`：错误码
- `result`：结果数据
- `result_size`：结果大小
- `user_data`：用户数据

#### uvrpc_response_free()

释放响应对象。

```c
void uvrpc_response_free(uvrpc_response_t* resp);
```

**参数**：
- `resp`：响应对象指针

## 回调类型

### uvrpc_handler_t

服务端处理函数类型。

```c
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);
```

### uvrpc_callback_t

客户端响应回调函数类型。

```c
typedef void (*uvrpc_callback_t)(uvrpc_response_t* resp, void* ctx);
```

### uvrpc_publish_callback_t

发布回调函数类型。

```c
typedef void (*uvrpc_publish_callback_t)(int status, void* ctx);
```

### uvrpc_subscribe_callback_t

订阅消息回调函数类型。

```c
typedef void (*uvrpc_subscribe_callback_t)(const char* topic, const uint8_t* data, size_t size, void* ctx);
```

### uvrpc_error_callback_t

错误回调函数类型。

```c
typedef void (*uvrpc_error_callback_t)(int error_code, const char* error_msg, void* ctx);
```

## 错误码

```c
#define UVRPC_OK 0
#define UVRPC_ERROR -1
#define UVRPC_ERROR_INVALID_PARAM -2
#define UVRPC_ERROR_NO_MEMORY -3
#define UVRPC_ERROR_NOT_CONNECTED -4
#define UVRPC_ERROR_TIMEOUT -5
#define UVRPC_ERROR_TRANSPORT -6
```

## 传输类型

```c
typedef enum {
    UVRPC_TRANSPORT_TCP = 0,
    UVRPC_TRANSPORT_UDP = 1,
    UVRPC_TRANSPORT_IPC = 2,
    UVRPC_TRANSPORT_INPROC = 3
} uvrpc_transport_type;
```

## 通信类型

```c
typedef enum {
    UVRPC_COMM_SERVER_CLIENT = 0,
    UVRPC_COMM_BROADCAST = 1
} uvrpc_comm_type_t;
```

## 内存分配器 API

### uvrpc_allocator_init()

初始化内存分配器。

```c
void uvrpc_allocator_init(uvrpc_allocator_type_t type, const uvrpc_custom_allocator_t* custom);
```

**参数**：
- `type`：分配器类型（UVRPC_ALLOCATOR_SYSTEM/MIMALLOC/CUSTOM）
- `custom`：自定义分配器（当 type 为 CUSTOM 时使用）

### uvrpc_allocator_cleanup()

清理内存分配器。

```c
void uvrpc_allocator_cleanup(void);
```

### uvrpc_allocator_get_type()

获取当前分配器类型。

```c
uvrpc_allocator_type_t uvrpc_allocator_get_type(void);
```

**返回值**：当前分配器类型

### uvrpc_allocator_get_name()

获取当前分配器名称。

```c
const char* uvrpc_allocator_get_name(void);
```

**返回值**：分配器名称字符串

## 内存分配器类型

```c
typedef enum {
    UVRPC_ALLOCATOR_SYSTEM = 0,
    UVRPC_ALLOCATOR_MIMALLOC = 1,
    UVRPC_ALLOCATOR_CUSTOM = 2
} uvrpc_allocator_type_t;
```

## 自定义分配器

```c
typedef struct {
    void* (*alloc)(size_t size);
    void* (*calloc)(size_t count, size_t size);
    void* (*realloc)(void* ptr, size_t size);
    void (*free)(void* ptr);
    char* (*strdup)(const char* s);
} uvrpc_custom_allocator_t;
```