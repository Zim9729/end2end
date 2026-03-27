# Jetson 部署与启动说明

## 1. 目标

本文档说明如何在 Jetson Orin Ubuntu 环境上部署当前 UDP 算法服务，并与存储主机联调。

当前方案：

- 共享目录传输入/输出 JSON 文件
- UDP 传任务提交和状态通知
- Orin 上运行一个接收进程和一个或多个 worker 进程

## 2. 环境信息

- 存储主机 IP：`192.168.145.1`
- Jetson Orin IP：`192.168.145.100`
- Jetson 系统：Ubuntu
- 推荐 UDP 监听端口：`9000`
- 软件侧回调端口：例如 `9001`

## 3. 目录规划

建议在 Jetson 上使用如下目录：

- 共享目录挂载点：`/mnt/rail_share`
- 本地队列目录：`/var/app/yolo_udp_queue`
- 程序目录：`/opt/yolo_udp_service`
- 日志目录：`/var/log/yolo_udp_service`

### 3.1 共享目录结构

共享目录中建议按如下组织：

- `requests/YYYY/MM/DD/...`
- `results/YYYY/MM/DD/...`

示例：

- `/mnt/rail_share/requests/2026/03/25/00001_1023.553_183701383.json`
- `/mnt/rail_share/results/2026/03/25/task-1_result.json`

### 3.2 本地队列结构

本地队列目录会由程序自动使用如下结构：

- `pending/`
- `working/`
- `done/`
- `failed/`
- `state/`

## 4. 共享目录挂载

根据现场共享方式不同，可采用 SMB/CIFS 或 NFS。这里给出 CIFS 示例。

### 4.1 手工挂载示例

```bash
sudo mkdir -p /mnt/rail_share
sudo mount -t cifs //192.168.145.1/rail_share /mnt/rail_share \
  -o username=<user>,password=<password>,iocharset=utf8,file_mode=0777,dir_mode=0777
```

### 4.2 开机自动挂载示例

编辑 `/etc/fstab`：

```fstab
//192.168.145.1/rail_share /mnt/rail_share cifs username=<user>,password=<password>,iocharset=utf8,file_mode=0777,dir_mode=0777 0 0
```

执行验证：

```bash
sudo mount -a
ls /mnt/rail_share
```

说明：

- 实际共享名、用户名、密码按现场配置修改
- 若现场使用 NFS，请改为 NFS 对应挂载方式

## 5. 程序部署

假设部署目录为：

- `/opt/yolo_udp_service/end2end`
- `/opt/yolo_udp_service/model.engine`

创建必要目录：

```bash
sudo mkdir -p /opt/yolo_udp_service
sudo mkdir -p /var/app/yolo_udp_queue
sudo mkdir -p /var/log/yolo_udp_service
```

建议把可执行文件、模型文件、配置说明统一放在 `/opt/yolo_udp_service`。

## 6. 启动命令

当前程序已经要求显式传入：

- UDP 监听端口
- 本地队列目录
- 共享根目录

### 6.1 启动接收进程

```bash
/opt/yolo_udp_service/end2end --serve 9000 /var/app/yolo_udp_queue /mnt/rail_share
```

### 6.2 启动 worker 进程

```bash
/opt/yolo_udp_service/end2end --worker /opt/yolo_udp_service/model.engine /var/app/yolo_udp_queue /mnt/rail_share
```

### 6.3 多 worker 示例

若显存和吞吐允许，可启动多个 worker：

```bash
/opt/yolo_udp_service/end2end --worker /opt/yolo_udp_service/model.engine /var/app/yolo_udp_queue /mnt/rail_share
/opt/yolo_udp_service/end2end --worker /opt/yolo_udp_service/model.engine /var/app/yolo_udp_queue /mnt/rail_share
```

建议：

- 第一阶段先从 `1` 个 worker 开始
- 压测稳定后再尝试增加到 `2` 个 worker
- 不建议一开始盲目开启过多进程

## 7. systemd 服务示例

### 7.1 receiver 服务

文件：`/etc/systemd/system/yolo-udp-receiver.service`

```ini
[Unit]
Description=YOLO UDP Receiver Service
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/yolo_udp_service
ExecStart=/opt/yolo_udp_service/end2end --serve 9000 /var/app/yolo_udp_queue /mnt/rail_share
Restart=always
RestartSec=2
StandardOutput=append:/var/log/yolo_udp_service/receiver.log
StandardError=append:/var/log/yolo_udp_service/receiver.err.log

[Install]
WantedBy=multi-user.target
```

### 7.2 worker 服务

文件：`/etc/systemd/system/yolo-udp-worker.service`

```ini
[Unit]
Description=YOLO UDP Worker Service
After=network.target yolo-udp-receiver.service

[Service]
Type=simple
WorkingDirectory=/opt/yolo_udp_service
ExecStart=/opt/yolo_udp_service/end2end --worker /opt/yolo_udp_service/model.engine /var/app/yolo_udp_queue /mnt/rail_share
Restart=always
RestartSec=2
StandardOutput=append:/var/log/yolo_udp_service/worker.log
StandardError=append:/var/log/yolo_udp_service/worker.err.log

[Install]
WantedBy=multi-user.target
```

### 7.3 启用服务

```bash
sudo systemctl daemon-reload
sudo systemctl enable yolo-udp-receiver.service
sudo systemctl enable yolo-udp-worker.service
sudo systemctl start yolo-udp-receiver.service
sudo systemctl start yolo-udp-worker.service
```

### 7.4 查看状态

```bash
sudo systemctl status yolo-udp-receiver.service
sudo systemctl status yolo-udp-worker.service
```

## 8. 联调检查清单

联调前建议按以下顺序检查：

### 8.1 网络检查

```bash
ping 192.168.145.1
ip addr
```

确认：

- Orin IP 为 `192.168.145.100`
- 能访问存储主机 `192.168.145.1`

### 8.2 共享目录检查

```bash
ls /mnt/rail_share
ls /mnt/rail_share/requests
ls /mnt/rail_share/results
```

确认：

- 挂载成功
- 有读写权限

### 8.3 UDP 监听检查

```bash
ss -unlp | grep 9000
```

确认：

- 接收服务已监听 `9000`

### 8.4 日志检查

```bash
tail -f /var/log/yolo_udp_service/receiver.log
tail -f /var/log/yolo_udp_service/worker.log
```

### 8.5 队列检查

```bash
find /var/app/yolo_udp_queue -maxdepth 2 -type f
```

确认：

- `pending/working/done/failed/state` 中有对应任务文件

## 9. 常见问题排查

### 9.1 收不到 `accepted`

检查：

- receiver 是否已启动
- 端口 `9000` 是否监听
- 软件侧提交 JSON 是否合法
- `reply_ip/reply_port` 是否正确
- 防火墙是否拦截 UDP

### 9.2 收到 `accepted` 但一直收不到 `done`

检查：

- worker 是否已启动
- queue 目录中任务是否卡在 `pending` 或 `working`
- engine 文件是否存在且可加载
- 输入 JSON 是否存在于共享目录
- `imagePath` 或 `image` 是否有效

### 9.3 结果文件没生成

检查：

- `/mnt/rail_share/results/...` 是否有写权限
- `result_relpath` 是否为空或非法
- worker 日志中是否有 `write result file failed`

### 9.4 图片解码失败

检查：

- `imagePath` 指向的文件是否存在
- `image` Base64 是否完整
- 输入 JSON 是否被软件侧完整写入后再提交 UDP

## 10. 部署建议

- 第一阶段先使用 `1 receiver + 1 worker`
- 压测稳定后再尝试 `1 receiver + 2 workers`
- TensorRT `.engine` 建议在 Jetson 本机生成
- 若现场需要长期运行，建议使用 systemd 托管
- 日志目录建议单独保留，便于排查现场问题

## 11. 推荐启动顺序

1. 挂载共享目录
2. 检查模型文件和可执行文件
3. 启动 receiver
4. 启动 worker
5. 软件侧发送测试任务
6. 检查 `accepted`
7. 检查 `done`
8. 检查结果 JSON 文件
