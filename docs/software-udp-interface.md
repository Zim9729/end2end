# 软件侧 UDP 联调接口文档

## 1. 目标

本文档用于说明存储主机软件与 Jetson Orin 算法服务之间的联调接口。

当前方案采用：

- 共享目录传输输入/输出 JSON 文件
- UDP 传输任务提交和状态通知

不使用 UDP 直接传输图片或大体积 JSON。

## 2. 网络角色

- 存储主机 IP：`192.168.145.1`
- Orin 算法服务 IP：`192.168.145.100`
- Orin UDP 监听端口：建议 `9000`
- 软件侧 UDP 回调监听端口：例如 `9001`

## 3. 共享目录约定

协议中只传共享目录下的相对路径，不传 Windows 或 Linux 绝对路径。

示例：

- 存储主机挂载路径：`Z:\rail_share`
- Orin 挂载路径：`/mnt/rail_share`

同一个文件：

- 主机本地路径：`Z:\rail_share\requests\2026\03\25\00001_1023.553_183701383.json`
- Orin 本地路径：`/mnt/rail_share/requests/2026/03/25/00001_1023.553_183701383.json`

协议中统一写：

- `requests/2026/03/25/00001_1023.553_183701383.json`
- `results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json`

### 3.1 路径规则

- 必须是相对路径
- 必须统一使用 `/`
- 不能带盘符，例如不能写 `D:/...`
- 不能写 Linux 绝对路径，例如不能写 `/mnt/...`
- 不能包含 `..`

## 4. 软件侧发送的 UDP 提交消息

软件侧向 `192.168.145.100:9000` 发送如下 JSON：

```json
{
  "cmd": "submit",
  "protocol_version": "1.0",
  "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
  "request_relpath": "requests/2026/03/25/00001_1023.553_183701383.json",
  "result_relpath": "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json",
  "reply_ip": "192.168.145.1",
  "reply_port": 9001
}
```

### 4.1 字段说明

| 字段 | 必填 | 说明 |
|---|---|---|
| `cmd` | 是 | 固定为 `submit` |
| `protocol_version` | 是 | 当前固定为 `1.0` |
| `task_id` | 是 | 任务唯一 ID，同一任务重试时必须保持不变 |
| `request_relpath` | 是 | 输入 JSON 在共享目录下的相对路径 |
| `result_relpath` | 建议是 | 输出 JSON 在共享目录下的相对路径 |
| `reply_ip` | 是 | 软件侧接收回包 IP，当前为 `192.168.145.1` |
| `reply_port` | 是 | 软件侧接收回包 UDP 端口，例如 `9001` |

## 5. Orin 返回的 UDP 消息

### 5.1 已接收回执

当 Orin 已成功接收并入队时，返回：

```json
{
  "cmd": "accepted",
  "protocol_version": "1.0",
  "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
  "status": "accepted",
  "result_relpath": "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json"
}
```

说明：

- 表示任务已被接收并成功进入处理队列
- 不代表算法已完成
- 软件侧收到此消息后，不应继续重复提交同一个 `task_id`

### 5.2 处理成功回执

当任务处理成功后，返回：

```json
{
  "cmd": "done",
  "protocol_version": "1.0",
  "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
  "status": "success",
  "result_relpath": "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json"
}
```

说明：

- 结果文件已写入共享目录
- 软件侧可据此去读取结果 JSON 文件

### 5.3 处理失败回执

当任务处理失败后，返回：

```json
{
  "cmd": "done",
  "protocol_version": "1.0",
  "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
  "status": "failed",
  "result_relpath": "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json",
  "error": "decode image failed"
}
```

说明：

- 任务处理结束，但失败
- `error` 为失败原因
- 软件侧按业务决定是否重试

### 5.4 返回字段说明

| 字段 | 说明 |
|---|---|
| `cmd` | `accepted` 或 `done` |
| `protocol_version` | 当前固定为 `1.0` |
| `task_id` | 对应的任务唯一 ID |
| `status` | `accepted`、`success`、`failed` |
| `result_relpath` | 结果文件相对路径 |
| `error` | 失败原因，仅 `failed` 时出现 |

## 6. 共享目录中的输入 JSON

软件侧需要先把输入 JSON 写入共享目录，再发送 UDP 提交消息。

示例路径：

- `requests/2026/03/25/00001_1023.553_183701383.json`

示例内容：

```json
{
  "device_id": "111f2246-0f54-45ce-89bc-80b72039718b",
  "id": "d5d32c980cbc44178550cb8a965e7e0b",
  "imagePath": "images/2026/03/25/00001_1023.553_183701389.jpg",
  "mileage": "1.250",
  "mileageSign": "K",
  "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
  "version": "1.0.0",
  "img_physical": 1.0,
  "img_scaling": 1.5,
  "image": "/9j/4AAQSkZJRgABAQAAAQABAAD/..."
}
```

说明：

- `task_id` 建议与 UDP 提交中的 `task_id` 保持一致
- `image` 可继续放 Base64 图片
- 当前服务会优先尝试使用 `imagePath` 指向的图片文件
- 若 `imagePath` 对应文件不可用，则回退使用 `image` 的 Base64 数据

## 7. 共享目录中的结果 JSON

示例路径：

- `results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json`

示例内容：

```json
{
  "defects": [
    {
      "id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5_1772950451323_0",
      "physical_ymax": 2047,
      "physical_ymin": 947,
      "type": "XLBH-542",
      "xmax": 2557,
      "xmin": 1502,
      "ymax": 2047,
      "ymin": 947
    }
  ],
  "ikoujian_count": 1,
  "imagePath": "images/2026/03/25/00001_1023.553_183701389.jpg",
  "img_physical": 1.0,
  "img_scaling": 1.5
}
```

## 8. 联调时序

1. 软件侧将输入 JSON 写入共享目录
2. 软件侧向 `192.168.145.100:9000` 发送 `submit`
3. Orin 返回 `accepted`
4. Orin worker 异步处理任务
5. Orin 返回 `done`
6. 软件侧根据 `result_relpath` 读取结果文件

## 9. 重试规则

- 若发送 `submit` 后在设定超时时间内未收到任何回包，可以重发
- 重发时必须使用相同的 `task_id`
- 收到 `accepted` 后不要再次重复提交
- 收到 `done + success` 后直接读取结果文件
- 收到 `done + failed` 后按业务决定是否重新提交

### 9.1 建议超时

- 等待 `accepted`：`500ms` 到 `2000ms`
- 超时重发次数：`1` 到 `3` 次
- 等待 `done`：根据算法耗时设置，例如 `10s` 到 `60s`

## 10. 常见错误文案

当前服务可能返回以下 `error`：

- `request_relpath is empty or invalid`
- `write state file failed`
- `write job file failed`
- `request file not found or empty`
- `image data is empty`
- `decode image failed`
- `imdecode image failed`
- `write result file failed`

建议软件侧直接记录原始错误文案，便于联调定位。

## 11. 推荐目录规范

建议按日期分目录，便于归档和排查：

- 输入目录：`requests/YYYY/MM/DD/...`
- 输出目录：`results/YYYY/MM/DD/...`

示例：

- `requests/2026/03/25/00001_1023.553_183701383.json`
- `results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json`

## 12. 注意事项

- UDP 中不要传输大图片或大 JSON
- 大数据只放共享目录
- `task_id` 是幂等键，不能在同一任务重试时变化
- `request_relpath` 和 `result_relpath` 必须是相对路径
- 软件侧与 Orin 侧需提前确认共享目录挂载正常
- 协议中不依赖 Windows 和 Linux 的本地绝对路径一致
