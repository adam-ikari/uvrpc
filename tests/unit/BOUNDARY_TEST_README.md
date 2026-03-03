# UVRPC 边界值测试文档

## 概述

边界值测试用于验证 UVRPC 框架在各种边界条件和极端情况下的行为。这些测试确保系统在处理最小值、最大值、空值和其他边界情况时能够正确运行。

## 测试套件

### 1. 简化边界值测试 (test_boundary_simple.c)

这个测试套件专注于验证流式响应 API 的边界条件，不涉及实际的网络通信。

#### 测试用例

1. **流结束检测 (type=1)**
   - 验证 `uvrpc_response_is_stream_end()` 正确识别 type=1
   - 验证 `uvrpc_response_is_stream_more()` 对 type=1 返回 false

2. **流更多检测 (type=2)**
   - 验证 `uvrpc_response_is_stream_more()` 正确识别 type=2
   - 验证 `uvrpc_response_is_stream_end()` 对 type=2 返回 false

3. **无效帧类型**
   - 验证无效的帧类型（如 99）被正确处理
   - 确保不会错误地识别为流结束或流更多

4. **NULL 响应**
   - 验证 NULL 指针处理
   - 确保不会崩溃或未定义行为

5. **空响应 (零大小)**
   - 验证 result_size=0 的响应
   - 确保空数据被正确处理

6. **单字节响应**
   - 验证最小非零数据大小 (1 byte)
   - 确保单字节数据被正确处理

7. **大响应 (10KB)**
   - 验证大尺寸数据的处理
   - 确保内存分配正确

8. **错误响应**
   - 验证错误状态的响应
   - 确保错误响应类型正确

9. **帧类型边界值**
   - 测试 type=0 (Request)
   - 测试 type=1 (Response)
   - 测试 type=2 (ResponseMore)
   - 测试 type=255 (最大字节值)

10. **结果大小边界值**
    - 测试 result_size=0
    - 测试 result_size=1
    - 测试 result_size=255
    - 测试 result_size=65535 (最大 uint16)

11. **消息 ID 边界值**
    - 测试 msgid=0
    - 测试 msgid=1
    - 测试 msgid=UINT32_MAX

12. **状态码边界值**
    - 测试 UVRPC_OK (0)
    - 测试 UVRPC_ERROR (-1)
    - 测试负数状态码

#### 测试结果

```
Total tests: 33
Passed: 33
Failed: 0
```

### 2. 流响应单元测试 (test_stream_response.c)

这个测试套件专注于流式响应功能的单元测试，包括编码、解码和帧类型检测。

#### 测试用例

1. **流结束检测**
2. **流更多检测**
3. **帧类型字段验证**
4. **NULL 响应处理**
5. **空响应处理**
6. **无效帧类型处理**
7. **编码请求**
8. **解码响应**
9. **编码流响应**
10. **编码错误响应**
11. **多块流响应**
12. **单块流响应**
13. **空数据流**

#### 测试结果

```
Total tests: 13
Passed: 13
Failed: 0
```

## 运行测试

### 运行所有边界值测试

```bash
cd tests/unit
./run_boundary_tests.sh
```

### 运行特定测试

```bash
cd tests/unit
./test_boundary_simple
./test_stream_response
```

## 边界值类别

### 数据大小边界

- **零字节**: 测试空数据和空参数
- **单字节**: 测试最小非零数据
- **大块数据**: 测试 10KB 数据块
- **最大值**: 测试 UINT16_MAX 和 UINT32_MAX

### 帧类型边界

- **type=0**: Request (客户端发送)
- **type=1**: Response (最后一个响应)
- **type=2**: ResponseMore (中间响应)
- **type=255**: 无效的最大字节值
- **type=99**: 随机无效值

### 状态码边界

- **UVRPC_OK (0)**: 成功状态
- **UVRPC_ERROR (-1)**: 一般错误
- **负数状态码**: 各种错误情况

### 指针边界

- **NULL 指针**: 验证空指针处理
- **有效指针**: 验证正常指针使用

## 测试覆盖的 API 函数

### 流式响应 API

- `uvrpc_response_is_stream_end()` - 检测流结束
- `uvrpc_response_is_stream_more()` - 检测流更多

### FlatBuffers 编码/解码 API

- `uvrpc_encode_request()` - 编码请求
- `uvrpc_decode_frame()` - 解码帧
- `uvrpc_encode_response()` - 编码响应
- `uvrpc_encode_response_more()` - 编码流响应
- `uvrpc_encode_error()` - 编码错误

## 测试最佳实践

1. **独立测试**: 每个测试用例独立运行，不依赖其他测试
2. **清晰断言**: 使用 TEST_ASSERT 宏提供清晰的失败信息
3. **边界优先**: 优先测试最小值、最大值和零值
4. **错误处理**: 验证错误情况被正确处理
5. **内存管理**: 确保分配的内存被正确释放

## 已知限制

1. **网络延迟**: 简化测试不涉及实际网络通信
2. **并发**: 当前测试主要是单线程
3. **性能**: 边界值测试不包含性能基准测试

## 未来改进

1. **集成测试**: 添加更多实际网络场景的边界测试
2. **并发测试**: 添加多线程并发边界测试
3. **压力测试**: 添加高负载下的边界条件测试
4. **性能测试**: 添加边界情况的性能测试

## 贡献指南

添加新的边界值测试时：

1. 确定要测试的边界条件
2. 创建独立的测试函数
3. 使用 TEST_ASSERT 宏进行断言
4. 添加清晰的测试描述
5. 更新此文档

## 参考资料

- [UVRPC API 文档](../../docs/API_REFERENCE.md)
- [单元测试指南](../../docs/QUICK_START.md)
- [FlatBuffers 文档](https://google.github.io/flatbuffers/)

---

**最后更新**: 2026-03-03  
**版本**: 1.0.0