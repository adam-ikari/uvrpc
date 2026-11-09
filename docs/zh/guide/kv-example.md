# 一个完整的应用：KV 服务

[快速开始](/zh/guide/quick-start) 里的示例是手写帧格式。真实项目不会这么做：自带 schema，
在构建时生成绑定，服务端与客户端是两个独立程序。这一页就是这个路径。

| 文件 | 作用 |
| --- | --- |
| `schema/kv.fbs` | 在 `KVService` 上声明 `Put` / `Get` / `Delete` |
| `examples/kv_server.c` | 实现三个处理器 |
| `examples/kv_client.c` | 调用生成的 API 并对结果逐条断言 |
| `tests/generated_api_test.c` | 同一套绑定在 `ctest` 下的回归测试（SAMELOOP） |

## Schema

```flatbuffers
namespace kv;

table PutRequest    { key: string; value: string; }
table PutResponse   { ok: bool; }
table GetRequest    { key: string; }
table GetResponse   { found: bool; value: string; }
table DeleteRequest { key: string; }
table DeleteResponse{ deleted: bool; }
```

构建时 `tools/uvrpcc.py` 读取它并生成绑定，产物不进仓库。`CMakeLists.txt` 里只需一行：

```cmake
uvrpc_dsl_generate(KV ${SCHEMA_DIR}/kv.fbs ${UVRPC_KV_GEN_DIR} kv_kvservice)
```

生成 `kv_kvservice_api.h`、`_client.c`、`_server_stub.c`、`_rpc_common.{h,c}`，
与 flatcc 自己的 `kv_reader.h` / `kv_builder.h` 放在同一目录。

## 构建与运行

```bash
cmake --build build --target kv_server kv_client
./dist/bin/kv_server            # 终端 1，默认 tcp://127.0.0.1:5555
./dist/bin/kv_client            # 终端 2
```

客户端每项检查打印一行，任一失败即非零退出：

```
kv_client: talking to tcp://127.0.0.1:5555
  Put(greeting) succeeds                     ok
  Get(greeting) returns hello, kv            ok
  Get(not-there) is found == false           ok
  Delete(greeting) reports deleted           ok
  Delete(greeting) again reports not deleted ok
  Get(greeting) is gone                      ok
  Put(value too long) is INVALID_PARAM       ok
kv_client: 0 failure(s)
```

## 生成绑定持有的是什么

有三件事决定了代码是能跑还是会踩内存，示例把三件都演示了。

**创建客户端不等待连接。** `create_client()` 发起连接就返回，结果从连接回调里来；
在那之前调用同步方法会返回 `UVRPC_ERROR_NOT_CONNECTED`。示例用 `uv_run(UV_RUN_NOWAIT)`
把循环泵到回调触发，并带超时。

**处理器拒绝必须以错误帧上报。** stub 在处理器返回非 `UVRPC_OK` 后调用
`uvrpc_response_send_error()`；若改用 `uvrpc_request_send_response()`，发出的是
**成功**帧，payload 为空。

**`_sync()` 交出的字符串字段是副本。** 响应缓冲在 `_sync()` 返回前就释放了，所以每个
字符串字段都是复制品，用生成的函数释放：

```c
kv_GetResponse_t response = {0};
uvrpc_error_t rc = uvrpc_kvservice_Get_sync(client, &response, &request, 5000);
if (rc == UVRPC_OK && response.found) {
    printf("%s\n", response.value);       /* 有效到下面这行为止 */
}
uvrpc_kvservice_Get_response_free(&response);
```

直接使用回调时契约相反：payload 只在回调执行期间有效，要留存必须当场复制。

组合调用遵循同一规则。`uvrpc_<svc>_<Method>_all()` 返回堆上的结构体数组，元素内是字符串
副本，用 `uvrpc_<svc>_<Method>_results_free(results, count)` 一次性释放（逐元素内容 + 数组
本身）；`uvrpc_<svc>_<Method>_any()` 写入调用方提供的结构体，用 `..._result_free()` 释放
内容，结构体本身归调用方。两者都只在返回 `UVRPC_OK` 时写入，调用前先零初始化。

## 同一套绑定在测试里

`tests/generated_api_test.c` 把生成的代码跑在 SAMELOOP 上——一种在同一进程内通过注册表
连接两端的传输。同步包装器内联驱动事件循环，顺带把服务端那一半也跑了，所以测试不需要端口，
也不需要第二个可执行文件。

```bash
ctest --test-dir build -R generated_api -V
```

它断言的就是示例展示的东西：同步路径能完成、返回的字符串在 `_sync()` 返回后仍可读、
释放函数把它置空、缺失键 `found == false`、被拒绝的 `Put` 以 `UVRPC_ERROR_INVALID_PARAM`
到达、两个组合调用交出的副本都能安全释放。
