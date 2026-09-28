# UVRPC Code Generator — Quick Guide

一键生成 RPC 代码的工具（`tools/uvrpcc.py`）。完整文档见 [UVRPCC.md](UVRPCC.md)。

## 快速开始

### 运行

```bash
# 直接从源码运行（需要 Python ≥ 3.8 + Jinja2）
pip install jinja2
python3 tools/uvrpcc.py schema/rpc.fbs -o generated

# 或通过 pyproject 安装后使用控制台入口
pip install -e .
uvrpc-gen schema/rpc.fbs -o generated
```

### 打包为独立可执行文件（可选）

```bash
pip install pyinstaller
cd tools && pyinstaller uvrpcc.spec --onefile
./tools/dist/uvrpc-gen --help
```

### 选项

```
uvrpc-gen [OPTIONS] SCHEMA

  --flatcc PATH       flatcc 编译器路径（未指定时自动探测 deps/flatcc/bin）
  -o, --output DIR    输出目录（默认: generated）
  -t, --templates DIR 模板目录（默认: tools/templates）
```

## Schema 格式

```flatbuffers
namespace myapp;

table AddRequest {
    a: int32;
    b: int32;
}

table AddResponse {
    result: int32;
}

// Server/Client 模式
rpc_service MathService {
    Add(AddRequest):AddResponse;
}

// Broadcast 模式（服务名包含 "Broadcast"）
rpc_service NewsBroadcastService {
    PublishNews(NewsPublishRequest):NewsPublishResponse;
}
```

## 生成的文件

```
generated/
├── myapp_math_api.h                  # API 头文件
├── myapp_math_rpc_common.h/.c        # 公共定义/工具
├── myapp_math_server_stub.c          # 服务端桩
├── myapp_math_client.c               # 客户端
├── myapp_builder.h / myapp_reader.h  # FlatCC 序列化
```

Broadcast 模式生成 `{ns}_{service}_api.h`、`_broadcast_publisher.c`、`_broadcast_subscriber.c`。

## 编译生成的代码

```bash
gcc -o server \
    myapp_math_server_stub.c myapp_math_rpc_common.c \
    -Igenerated -Iinclude \
    -Ldist/lib -l:libuvrpc_full.a -lpthread -lm -lrt

gcc -o client \
    myapp_math_client.c myapp_math_rpc_common.c \
    -Igenerated -Iinclude \
    -Ldist/lib -l:libuvrpc_full.a -lpthread -lm -lrt
```

`libuvrpc_full.a` 已合并 libuv/flatcc/mimalloc，无需再单独链接 `-luv -lmimalloc`。

## 常见问题

**Q: 需要 Python 吗？**
A: 从源码运行或 `pip install` 需要；PyInstaller 打包后的可执行文件不需要。

**Q: 需要 FlatCC 吗？**
A: 生成器会自动调用 flatcc（先运行 `./scripts/setup_deps.sh` 构建到 `deps/flatcc/bin`，或用 `--flatcc` 指定路径）。

**Q: 支持哪些传输方式？**
A: TCP、UDP、IPC、INPROC（进程内）、SAMELOOP。

**Q: 支持广播模式吗？**
A: 支持，服务名包含 "Broadcast" 即可。

## 更多信息

- 生成器完整文档: `tools/UVRPCC.md`
- 快速构建: `./quickstart.sh`
- 构建与安装: `docs/build-install.md`
