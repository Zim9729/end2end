# caller_client_cpp

`caller_client_cpp` 是一个独立的 C++ 控制台调用方示例工程，用于对接当前 `end2end` 项目的 UDP 服务。

它完成以下链路：

- 读取本地请求模板 JSON
- 将请求 JSON 写入共享目录
- 发送 UDP `submit`
- 等待 `accepted`
- 继续等待 `done`
- 按 `result_relpath` 读取结果 JSON

## 目录结构

```text
caller_client_cpp/
  CMakeLists.txt
  README.md
  docs/
    integration-guide.md
  examples/
    request_template.json
  include/
    caller_client/
      client.hpp
      file_utils.hpp
      protocol.hpp
  src/
    client.cpp
    file_utils.cpp
    main.cpp
    protocol.cpp
  tests/
    protocol_test.cpp
```

## 构建

### Windows

```powershell
cmake -S caller_client_cpp -B caller_client_cpp/build
cmake --build caller_client_cpp/build --config Release
```

### Linux

```bash
cmake -S caller_client_cpp -B caller_client_cpp/build
cmake --build caller_client_cpp/build -j
```

## 运行示例

```powershell
caller_client_cpp\build\Release\caller_client_demo ^
  --server-ip 192.168.145.100 ^
  --server-port 9000 ^
  --reply-ip 192.168.145.1 ^
  --reply-port 9001 ^
  --shared-root Z:\rail_share ^
  --source-request caller_client_cpp\examples\request_template.json
```

## 参数说明

- `--server-ip`
- `--server-port`
- `--reply-ip`
- `--reply-port`
- `--shared-root`
- `--source-request`
- `--task-id`
- `--request-relpath`
- `--result-relpath`
- `--accepted-timeout-ms`
- `--done-timeout-ms`
- `--retry-count`

详细接入说明见 `docs/integration-guide.md`。
