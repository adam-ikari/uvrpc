# YAML RPC 服务 DSL 语法

## 概述

uvrpc 使用 YAML 格式定义 RPC 服务，提供简洁易读的服务描述语言。

## 基本结构

```yaml
# 服务定义
service: "服务名称"
version: "版本号"
description: "服务描述"

# 方法列表
methods:
  - name: "方法名称"
    description: "方法描述"
    request:
      # 请求参数定义
      type: "参数类型"  # string, int, float, bool, bytes, array, map
      required: true   # 是否必需
      default: null    # 默认值
    response:
      # 响应参数定义
      type: "参数类型"
      description: "参数描述"
```

## 完整示例

```yaml
# Echo 服务定义
service: "EchoService"
version: "1.0.0"
description: "简单的回显服务"

methods:
  - name: "echo"
    description: "回显输入的消息"
    request:
      type: "map"
      fields:
        - name: "message"
          type: "string"
          required: true
          description: "要回显的消息"
    response:
      type: "map"
      fields:
        - name: "echo"
          type: "string"
          description: "回显的消息"
        - name: "timestamp"
          type: "int"
          description: "时间戳"

  - name: "add"
    description: "计算两个数字的和"
    request:
      type: "map"
      fields:
        - name: "a"
          type: "float"
          required: true
          description: "第一个数字"
        - name: "b"
          type: "float"
          required: true
          description: "第二个数字"
    response:
      type: "map"
      fields:
        - name: "result"
          type: "float"
          description: "计算结果"

  - name: "get_user"
    description: "获取用户信息"
    request:
      type: "map"
      fields:
        - name: "user_id"
          type: "int"
          required: true
          description: "用户 ID"
    response:
      type: "map"
      fields:
        - name: "id"
          type: "int"
          description: "用户 ID"
        - name: "name"
          type: "string"
          description: "用户名"
        - name: "email"
          type: "string"
          description: "邮箱"
        - name: "active"
          type: "bool"
          description: "是否活跃"
```

## 类型系统

### 基本类型

| 类型 | 描述 | 示例 |
|------|------|------|
| `string` | 字符串 | `"hello"` |
| `int` | 整数 | `42` |
| `float` | 浮点数 | `3.14` |
| `bool` | 布尔值 | `true`, `false` |
| `bytes` | 二进制数据 | `base64编码字符串` |

### 复合类型

| 类型 | 描述 | 示例 |
|------|------|------|
| `array` | 数组 | `[1, 2, 3]` |
| `map` | 映射 | `{"key": "value"}` |

## 字段属性

### 必需字段

```yaml
request:
  type: "map"
  fields:
    - name: "username"
      type: "string"
      required: true
```

### 可选字段

```yaml
request:
  type: "map"
  fields:
    - name: "email"
      type: "string"
      required: false
      default: null
```

### 默认值

```yaml
request:
  type: "map"
  fields:
    - name: "timeout"
      type: "int"
      default: 30000
```

## 服务元数据

```yaml
service: "UserService"
version: "2.1.0"
description: "用户管理服务"
author: "开发团队"
license: "MIT"
```

## 方法元数据

```yaml
methods:
  - name: "create_user"
    description: "创建新用户"
    deprecated: false
    timeout: 5000  # 超时时间（毫秒）
    rate_limit:    # 速率限制
      max_requests: 100
      time_window: 60  # 秒
```

## 示例：复杂服务定义

```yaml
service: "FileService"
version: "1.0.0"
description: "文件操作服务"

methods:
  - name: "upload"
    description: "上传文件"
    request:
      type: "map"
      fields:
        - name: "filename"
          type: "string"
          required: true
        - name: "content"
          type: "bytes"
          required: true
        - name: "compress"
          type: "bool"
          default: false
    response:
      type: "map"
      fields:
        - name: "file_id"
          type: "string"
        - name: "size"
          type: "int"
        - name: "compressed"
          type: "bool"

  - name: "download"
    description: "下载文件"
    request:
      type: "map"
      fields:
        - name: "file_id"
          type: "string"
          required: true
    response:
      type: "map"
      fields:
        - name: "filename"
          type: "string"
        - name: "content"
          type: "bytes"
        - name: "size"
          type: "int"

  - name: "list_files"
    description: "列出所有文件"
    request:
      type: "map"
      fields:
        - name: "limit"
          type: "int"
          default: 100
        - name: "offset"
          type: "int"
          default: 0
    response:
      type: "map"
      fields:
        - name: "total"
          type: "int"
        - name: "files"
          type: "array"
          item_type: "map"
          item_fields:
            - name: "file_id"
              type: "string"
            - name: "filename"
              type: "string"
            - name: "size"
              type: "int"
```

## 使用 DSL

1. 创建 YAML 文件定义服务
2. 使用 `uvrpc-gen` 工具生成代码
3. 在服务器端注册服务处理器
4. 在客户端调用服务方法

## 代码生成

```bash
uvrpc-gen --yaml service.yaml --output ./generated
```

生成的代码包含：
- 服务定义结构体
- 序列化/反序列化函数
- 服务器端接口
- 客户端接口