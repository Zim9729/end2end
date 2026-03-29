# 调用方集成指导

## 1. 目标

本文档面向调用方软件，说明如何使用 `caller_client_cpp` 对接 Jetson 上的 UDP 算法服务。

当前架构为：

- 共享目录传请求文件和结果文件
- UDP 只传任务通知与状态回包

## 2. 部署关系

### 2.1 调用方主机

调用方主机负责：

- 生成业务请求 JSON
- 将请求 JSON 写入共享目录
- 向 Jetson `9000` 发送 `submit`
- 在本机 `reply_port` 接收 `accepted` / `done`
- 按 `result_relpath` 读取结果文件

### 2.2 Jetson 主机

Jetson 主机负责：

- 运行 `receiver`
- 运行 `worker`
- 从共享目录读取输入 JSON
- 将结果 JSON 写回共享目录

### 2.3 queue 与 shared_root

- `queue_dir` 在 Jetson 主机本地
- `shared_root` 为调用方和 Jetson 共同访问的共享目录

调用方不需要访问 Jetson 的 `queue_dir`。

## 3. 共享目录约定

协议中统一使用共享目录相对路径。

示例：

- 调用方挂载路径：`Z:\rail_share`
- Jetson 挂载路径：`/mnt/rail_share`

协议中只传：

- `requests/2026/03/30/task-1.json`
- `results/2026/03/30/task-1_result.json`

### 3.1 路径规则

- 必须是相对路径
- 必须统一使用 `/`
- 不能包含盘符
- 不能是绝对路径
- 不能包含 `..`

## 4. UDP 消息

### 4.1 提交消息

```json
{
  "cmd": "submit",
  "protocol_version": "1.0",
  "task_id": "task-1",
  "request_relpath": "requests/2026/03/30/task-1.json",
  "result_relpath": "results/2026/03/30/task-1_result.json",
  "reply_ip": "192.168.145.1",
  "reply_port": 9001
}
```

### 4.2 回包语义

- `accepted`
  - 代表任务已入队
  - 不代表算法已完成

- `done + success`
  - 代表结果文件已经写入共享目录
  - 调用方应读取 `result_relpath`

- `done + failed`
  - 代表任务结束但失败
  - 调用方应记录 `error`

## 5. `caller_client_demo` 做了什么

程序执行流程如下：

1. 读取 `--source-request` 指定的本地模板 JSON
2. 自动写入或覆盖模板中的 `task_id`
3. 自动生成或使用指定的 `request_relpath`
4. 自动生成或使用指定的 `result_relpath`
5. 将请求文件写入 `shared_root/request_relpath`
6. 向 Jetson 发送 UDP `submit`
7. 等待 `accepted`
8. 继续等待 `done`
9. 若成功，则读取 `shared_root/result_relpath`
10. 输出结果 JSON

## 6. 命令行参数

### 必填参数

- `--server-ip`
- `--server-port`
- `--reply-ip`
- `--reply-port`
- `--shared-root`
- `--source-request`

### 可选参数

- `--task-id`
- `--request-relpath`
- `--result-relpath`
- `--accepted-timeout-ms`
- `--done-timeout-ms`
- `--retry-count`

## 7. 使用步骤

### 步骤 1：准备共享目录

确认调用方与 Jetson 都能访问同一个共享目录。

### 步骤 2：准备请求模板

参考 `examples/request_template.json`。

注意：

- 如果使用 `imagePath`，该路径最终要能被 Jetson 读取到
- 如果 `imagePath` 是相对路径，它将相对于共享目录中的请求 JSON 所在目录解析
- 如果不方便共享图片文件，可以直接在 `image` 中放 Base64

### 步骤 3：运行调用方程序

```powershell
caller_client_cpp\build\Release\caller_client_demo ^
  --server-ip 192.168.145.100 ^
  --server-port 9000 ^
  --reply-ip 192.168.145.1 ^
  --reply-port 9001 ^
  --shared-root Z:\rail_share ^
  --source-request caller_client_cpp\examples\request_template.json
```

### 步骤 4：观察输出

正常情况下会输出：

- `task_id`
- `request_relpath`
- `result_relpath`
- `request_file`
- `accepted_reply`
- `done_reply`
- `result_file`
- 结果 JSON 正文

## 8. 重试与幂等建议

- 如果短时间没有收到任何回包，可以重发同一个 `task_id`
- 收到 `accepted` 后不要再重复提交同一个 `task_id`
- 如果你想重新处理同一份数据，应该使用新的 `task_id`

## 9. 常见失败原因

服务端当前可能返回：

- `request_relpath is empty or invalid`
- `write state file failed`
- `write job file failed`
- `request file not found or empty`
- `image data is empty`
- `decode image failed`
- `imdecode image failed`
- `write result file failed`

调用方建议直接记录原始 `error`。

## 10. 排障建议

### 未收到 `accepted`

检查：

- Jetson `receiver` 是否启动
- `server_ip/server_port` 是否正确
- 本机 `reply_port` 是否被占用
- 防火墙是否放行 UDP

### 收到 `accepted` 但未收到 `done`

检查：

- Jetson `worker` 是否启动
- 模型文件是否正常
- 共享目录是否正常挂载
- 请求 JSON 是否已经成功写入共享目录
- 请求 JSON 中的 `imagePath` 或 `image` 是否有效

### 收到 `done + success` 但结果文件为空

检查：

- `result_relpath` 对应的共享目录文件是否实际生成
- 调用方读取的是共享目录路径，不是协议相对路径

## 11. 推荐接入方式

如果后续要集成到正式业务软件，建议保留以下边界：

- 业务层只负责准备请求数据
- 文件落盘、UDP 提交、回包等待、结果读取封装在独立模块中
- `task_id` 作为幂等键统一管理

这样后续替换 UI、上位机或服务宿主时，协议适配层可以保持稳定。
