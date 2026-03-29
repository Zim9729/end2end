# end2end YOLOv8 推理工程

这是一个基于 **C++ / CUDA / TensorRT / OpenCV** 的 YOLOv8 推理工程，支持以下能力：

- 本地图片 / 图片目录 / 视频批量推理
- UDP 提交任务、接收回执、轮询 worker 结果
- 共享目录传输输入与结果 JSON
- Windows 开发调试，Jetson Ubuntu 部署运行

## 1. 项目目标

项目当前面向两种运行场景：

- **Windows 开发环境**
  - 编译、调试、验证协议和结果 JSON
  - 跑单测，确认协议解析和结果格式正确

- **Jetson Ubuntu 运行环境**
  - 运行 UDP 服务和 worker
  - 接收来自存储主机的任务通知
  - 在共享目录中读取输入 JSON 和图片数据，写回结果 JSON

## 2. 目录结构

```text
end2end/
├── include/                 # 对外头文件
├── src/                     # 实现文件
├── tests/                   # 单元测试
├── docs/                    # 协议说明、联调文档
├── 3rdparty/                # 第三方依赖
├── CMakeLists.txt           # CMake 构建脚本
└── README.md
```

主要文件说明：

- `src/main.cpp`
  - 程序入口
  - 支持批量推理、UDP 服务、worker 三种模式
- `src/udp_service.cpp`
  - UDP 接收、任务投递、worker 处理主流程
- `src/service_protocol.cpp`
  - JSON 解析、结果 JSON 生成、任务状态 JSON 生成
- `tests/service_protocol_test.cpp`
  - 协议层单测
- `docs/software-udp-interface.md`
  - UDP 协议与共享目录联调说明

## 3. 功能说明

### 3.1 批量推理

支持对单张图片、图片目录、视频进行推理，输出结果图像。

### 3.2 UDP + 共享目录联动

当前推荐方案是：

- **共享目录** 传输输入 / 输出 JSON
- **UDP** 只传任务提交和结果回执

这样可以避免 UDP 直接承载大体积 Base64 图片数据。

### 3.3 结果 JSON

结果 JSON 由 `service_protocol.cpp` 生成，包含：

- `defects`
- `ikoujian_count`
- `imagePath`
- `img_physical`
- `img_scaling`

缺陷对象包含：

- `id`
- `physical_ymax`
- `physical_ymin`
- `type`
- `xmax`
- `xmin`
- `ymax`
- `ymin`

## 4. 跨平台说明

本工程采用 **同一套源码**，分别在 Windows 和 Jetson Ubuntu 上构建。

### 4.1 跨平台边界

- **跨平台的部分**
  - 协议解析与 JSON 生成
  - 共享目录路径逻辑
  - UDP 任务提交 / 回执协议
  - 单元测试

- **按平台分别处理的部分**
  - CUDA / TensorRT 安装路径
  - TensorRT engine 文件
  - Socket API 的平台差异

### 4.2 重要提醒

- Windows 上生成的 TensorRT `engine` **通常不能直接拿到 Jetson Ubuntu 上使用**
- Jetson 端应使用 Jetson 环境重新构建 engine
- 共享目录建议使用相对路径，不要在协议中传 Windows 盘符或 Linux 绝对路径

## 5. 依赖环境

### 5.1 Windows 开发环境

建议准备：

- Visual Studio 2019 / 2022
- CMake 3.20+
- CUDA Toolkit 11.8
- OpenCV 4.x
- TensorRT 8.5.2.2

### 5.2 Jetson Ubuntu 运行环境

建议准备：

- Jetson Orin / JetPack 对应的 Ubuntu 环境
- CUDA
- TensorRT
- OpenCV
- CMake
- 可用的共享目录挂载

## 6. Windows 构建

### 6.1 配置

在项目根目录执行：

```bash
cmake -S . -B build
```

### 6.2 编译

```bash
cmake --build build --config Release
```

### 6.3 运行单测

```bash
cmake --build build --config Release --target service_protocol_test
build\Release\service_protocol_test.exe
```

## 7. Jetson Ubuntu 构建

### 7.1 配置

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### 7.2 编译

```bash
cmake --build build -j
```

### 7.3 运行单测

```bash
cmake --build build --target service_protocol_test -j
./build/service_protocol_test
```

## 8. 运行方式

程序支持三种模式。

### 8.1 批量推理模式

```bash
end2end <engine_file> <image_or_dir>
```

说明：

- `engine_file`：TensorRT engine 文件
- `image_or_dir`：单张图片、图片目录或视频文件

### 8.2 UDP 服务模式

```bash
end2end --serve <listen_port> <queue_dir> <shared_root>
```

例如：

```bash
end2end --serve 9000 queue shared
```

### 8.3 Worker 模式

```bash
end2end --worker <engine_file> <queue_dir> <shared_root>
```

例如：

```bash
end2end --worker yolov8.engine queue shared
```

### 8.4 UDP 客户端 demo

新增了一个独立的 UDP 调用客户端 demo，用于向 Jetson UDP 服务发送 `submit` 请求并接收回执。

构建命令：

```bash
cmake --build build --config Release --target udp_client_demo
```

运行方式：

```bash
udp_client_demo <server_ip> <server_port> <reply_ip> <reply_port> [task_id] [request_relpath] [result_relpath]
```

参数说明：

- `server_ip`：Jetson Orin UDP 服务地址，例如 `192.168.145.100`
- `server_port`：Jetson UDP 监听端口，例如 `9000`
- `reply_ip`：回执中填写的本机 IP，例如 `192.168.145.1`
- `reply_port`：本机接收回执的端口，例如 `9001`
- `task_id`：可选，任务 ID，不传时自动生成
- `request_relpath`：可选，共享目录中的输入 JSON 相对路径
- `result_relpath`：可选，共享目录中的结果 JSON 相对路径

示例：

```bash
udp_client_demo 192.168.145.100 9000 192.168.145.1 9001 task-1 requests/inbox/a.json results/outbox/a_result.json
```

该 demo 会发送如下 `submit` 请求字段：

- `cmd=submit`
- `protocol_version=1.0`
- `task_id`
- `request_relpath`
- `result_relpath`
- `reply_ip`
- `reply_port`

#### 本地测试方式

如果你只是想先在本机验证 UDP 通信是否正常，可以用回环地址做最小联通测试：

1. 启动 UDP 服务：

```bash
end2end --serve 9000 queue shared
```

2. 另开一个终端运行客户端 demo：

```bash
udp_client_demo 127.0.0.1 9000 127.0.0.1 9001 task-1 requests/inbox/a.json results/outbox/a_result.json
```

3. 预期结果：

- 客户端打印发送的 JSON
- 服务端返回 `accepted`
- 客户端打印回包内容

如果要验证完整链路，还需要再启动 `worker` 模式，并检查共享目录里任务文件和结果文件是否生成。

## 9. UDP 联调流程

推荐的联调方式如下：

1. 存储主机把输入 JSON 写到共享目录
2. 存储主机向 Jetson UDP 服务发送 `submit`
3. Jetson 返回 `accepted`
4. Worker 在共享目录中读取任务
5. Worker 执行推理并写回结果 JSON
6. Jetson 返回 `done`
7. 存储主机读取结果 JSON

### 9.1 示例地址

- 存储主机 IP：`192.168.145.1`
- Jetson Orin IP：`192.168.145.100`
- Jetson UDP 监听端口：`9000`
- 存储主机回调端口：例如 `9001`

## 10. 输入 / 输出 JSON

具体格式请参考：

- `docs/software-udp-interface.md`

其中包含：

- 提交消息 JSON 示例
- 输入 JSON 示例
- 结果 JSON 示例
- 联调时序说明

## 11. 单元测试

当前已有协议层单测：

- `tests/service_protocol_test.cpp`

可验证内容包括：

- UDP 提交消息解析
- 输入 JSON 解析
- Base64 解码
- 业务类别映射
- `physical_y` 计算
- 结果 JSON 格式
- 任务状态 JSON 格式

## 12. 常见问题

### 12.1 `CUDAToolkit` 找不到

请确认已安装 CUDA Toolkit，并且 CMake 能找到对应环境。

### 12.2 TensorRT include 路径找不到

请确认 TensorRT 安装位置正确，或者调整 `CMakeLists.txt` 中的路径配置。

### 12.3 Jetson 和 Windows 的 engine 能不能通用

一般**不能通用**。建议在目标设备上重新生成 engine。

## 13. 文档索引

- `docs/software-udp-interface.md`
- `docs/code-reading-guide.md`

## 14. 许可证

当前项目未单独声明许可证，默认按项目内部使用处理。
