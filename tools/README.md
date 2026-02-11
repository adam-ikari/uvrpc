# uvrpc DSL 代码生成器

基于 Node.js、jsyaml 和 Nunjucks 的 YAML DSL 解析器和代码生成器。

## 功能

- 解析 YAML 格式的 RPC 服务定义
- 自动生成 C 语言的序列化/反序列化代码
- 生成服务器端和客户端示例代码
- 使用 msgpack 二进制格式进行序列化

## 安装

```bash
cd tools
npm install
```

## 使用方法

### 基本用法

```bash
node cli.js --yaml <yaml文件路径> --output <输出目录>
```

### 示例

```bash
# 生成 EchoService 的代码
node cli.js --yaml ../examples/echo_service.yaml --output ../generated
```

### 使用 npm 脚本

```bash
npm test
```

## 输出文件

代码生成器会生成以下文件：

- `<服务名>_gen.h` - 头文件，包含结构体定义和函数声明
- `<服务名>_gen.c` - 源文件，包含序列化/反序列化实现
- `<服务名>_server_example.c` - 服务器端示例代码
- `<服务名>_client_example.c` - 客户端示例代码

## YAML DSL 格式

### 基本结构

```yaml
service: "服务名称"
version: "版本号"
description: "服务描述"

methods:
  - name: "方法名称"
    description: "方法描述"
    request:
      type: "map"
      fields:
        - name: "字段名"
          type: "字段类型"
          required: true
          description: "字段描述"
    response:
      type: "map"
      fields:
        - name: "字段名"
          type: "字段类型"
          description: "字段描述"
```

### 支持的类型

- `string` - 字符串
- `int` - 整数
- `float` - 浮点数
- `bool` - 布尔值
- `bytes` - 二进制数据
- `array` - 数组
- `map` - 映射

### 完整示例

参见 `../examples/echo_service.yaml`

## 生成的代码

### 结构体

每个方法的请求和响应都会生成对应的结构体：

```c
typedef struct EchoService_echo_Request {
    char* message;    /* 要回显的消息 */
} EchoService_echo_Request_t;

typedef struct EchoService_echo_Response {
    char* echo;    /* 回显的消息 */
    int64_t timestamp;    /* 时间戳（毫秒） */
} EchoService_echo_Response_t;
```

### 序列化函数

每个方法的请求和响应都会生成序列化和反序列化函数：

```c
int EchoService_echo_SerializeRequest(
    const EchoService_echo_Request_t* request,
    uint8_t** output,
    size_t* output_size
);

int EchoService_echo_DeserializeRequest(
    const uint8_t* data,
    size_t size,
    EchoService_echo_Request_t* request
);
```

### 内存管理函数

每个方法都会生成内存释放函数：

```c
void EchoService_echo_FreeRequest(EchoService_echo_Request_t* request);
void EchoService_echo_FreeResponse(EchoService_echo_Response_t* response);
```

## 依赖

- Node.js >= 14
- js-yaml - YAML 解析器
- nunjucks - 模板引擎
- commander - 命令行参数解析

## 开发

### 文件结构

```
tools/
├── package.json          # npm 配置
├── cli.js                # 命令行入口
├── parser.js             # YAML 解析器
├── generator.js          # 代码生成器
└── templates/            # Nunjucks 模板
    ├── header.njk        # 头文件模板
    └── source.njk        # 源文件模板
```

### 添加新功能

1. 在 `parser.js` 中添加新的解析方法
2. 在 `generator.js` 中添加新的代码生成逻辑
3. 在 `templates/` 中创建或修改模板

## 注意事项

- 生成的代码不应该手动编辑
- 每次运行生成器会覆盖已生成的文件
- 确保输入的 YAML 文件格式正确
- 复杂的类型（如嵌套的 map 和 array）可能需要额外的处理

## 故障排除

### 错误：缺少必需字段

确保 YAML 文件包含 `service`、`version` 和 `methods` 字段。

### 错误：方法定义不完整

每个方法必须包含 `request` 和 `response` 定义。

### 错误：生成代码编译失败

检查生成的代码是否符合 C99 标准，并确保正确包含了 mpack 头文件。

## 许可证

MIT License