# 读代码导航版

本文档用于快速理解当前 UDP 服务实现中几个关键文件和关键函数之间的关系，便于后续维护、联调和排查问题。

## 1. 总体文件关系

核心文件：

- `main.cpp`
- `udp_service.cpp`
- `service_protocol.cpp`
- `tests/service_protocol_test.cpp`

职责概览：

- `main.cpp`：程序入口，负责模式分发
- `udp_service.cpp`：UDP 接收、任务队列、worker 主流程、推理调度
- `service_protocol.cpp`：JSON 解析、结果 JSON 构造、状态回包、Base64 解码
- `service_protocol_test.cpp`：协议层单元测试

## 2. 程序入口

从 `main.cpp` 开始：

- `--serve` 模式进入 `RunUdpReceiver(...)`
- `--worker` 模式进入 `RunWorkerLoop(...)`
- 普通模式进入 `RunBatchInference(...)`

如果你在看 UDP 服务，重点关注：

- `RunUdpReceiver(...)`
- `RunWorkerLoop(...)`

## 3. 接收端入口

位置：`udp_service.cpp` 中的 `RunUdpReceiver(...)`

关键步骤：

1. 检查并创建本地队列目录
2. 检查共享目录 `shared_root`
3. 绑定 UDP 监听端口
4. 接收软件侧发来的 `submit` JSON
5. 调 `ParseSubmitRequest(...)` 解析协议
6. 规范化 `request_relpath` 和 `result_relpath`
7. 用 `ReadTaskState(...)` 做幂等检查
8. 若任务已存在，直接按当前状态回包
9. 若任务不存在：
   - 写初始状态文件
   - 写 `pending` 任务文件
   - 回 `accepted`

适合排查的问题：

- 为什么收不到 `accepted`
- 为什么重复任务会重复执行
- 为什么提交路径被判非法

## 4. 协议解析入口

位置：`service_protocol.cpp` 中的 `ParseSubmitRequest(...)`

负责从 UDP JSON 中解析：

- `cmd`
- `protocol_version`
- `task_id`
- `request_relpath`
- `result_relpath`
- `reply_ip`
- `reply_port`

兼容逻辑：

- 若没有 `request_relpath/result_relpath`，会回退读取旧字段 `request_file/result_file`

适合排查的问题：

- 软件侧发来的字段为什么没被识别
- 旧协议是否仍兼容

## 5. 路径安全与共享目录解析

位置：`udp_service.cpp`

关键函数：

- `NormalizeSlashes(...)`
- `IsAbsolutePath(...)`
- `ContainsParentTraversal(...)`
- `NormalizeRelativePath(...)`
- `ResolveSharedPath(...)`

用途：

- 把软件侧路径统一成 `/`
- 禁止绝对路径
- 禁止 `..` 路径穿越
- 把共享目录相对路径拼成 Orin 本地真实路径

适合排查的问题：

- 为什么 `request_relpath` 被判定非法
- 为什么 Orin 找不到共享目录文件

## 6. 状态文件和幂等逻辑

位置：`udp_service.cpp`

关键函数：

- `BuildStateFilePath(...)`
- `ReadTaskState(...)`
- `WriteTaskState(...)`
- `MakeStateFromRequest(...)`
- `BuildReplyForState(...)`

用途：

- 按 `task_id` 维护 `state/<task_id>.json`
- 重复提交时直接返回已有状态
- 区分 `accepted / pending / working / success / failed`

适合排查的问题：

- 同一个 `task_id` 为什么被重复处理
- 当前任务到底处于哪个状态

## 7. worker 主循环

位置：`udp_service.cpp` 中的 `RunWorkerLoop(...)`

关键步骤：

1. 扫描 `pending/*.json`
2. 原子重命名到 `working`
3. 解析任务内容
4. 写状态为 `working`
5. 调 `ProcessTask(...)`
6. 根据结果更新状态为 `success` 或 `failed`
7. 发送 `done`

适合排查的问题：

- 为什么任务已入队但一直不处理
- 为什么任务卡在 `pending`
- 为什么任务卡在 `working`

## 8. 单任务执行核心

位置：`udp_service.cpp` 中的 `ProcessTask(...)`

这是单任务真正执行的核心函数，流程如下：

1. 把 `request_relpath` 和 `result_relpath` 解析成共享目录真实路径
2. 读取输入 JSON 文件
3. 调 `ParseInputRequest(...)` 解析输入参数
4. 调 `DecodeImage(...)` 解码图片
5. 调 YOLOv8 推理
6. 调 `BuildDefects(...)` 生成业务结果结构
7. 调 `BuildResultJson(...)` 生成最终 JSON
8. 把结果 JSON 写入共享目录

适合排查的问题：

- 为什么结果文件没有生成
- 为什么 worker 执行失败
- 为什么图片解码失败

## 9. 图片来源逻辑

位置：`udp_service.cpp`

关键函数：

- `ResolveImagePath(...)`
- `DecodeImage(...)`

规则：

1. 优先尝试读取 `imagePath` 指向的图片文件
2. 若文件不可用，再回退到 `image` Base64 数据

适合排查的问题：

- 软件侧明明写了图片，为什么解码失败
- `imagePath` 和 Base64 究竟哪个生效

## 10. 结果 JSON 生成

分两层：

### 10.1 结构转换层

位置：`udp_service.cpp` 中的 `BuildDefects(...)`

负责把模型输出 `Object` 转换成 `DefectRecord`，包括：

- `type`
- `xmin`
- `ymin`
- `xmax`
- `ymax`
- `physical_ymin`
- `physical_ymax`

### 10.2 JSON 拼装层

位置：`service_protocol.cpp` 中的 `BuildResultJson(...)`

负责把 `DefectRecord` 数组真正输出成结果 JSON 文本。

适合排查的问题：

- 为什么结果字段结构不对
- 为什么 `type` 不符合软件侧预期
- 为什么 `physical_y` 不对

## 11. 当前最可能继续改动的位置

如果后续需要继续优化业务输出，优先看这里：

- `udp_service.cpp` 中 `BuildDefects(...)`
  - 当前 `type` 来源于 `CLASS_NAMES[object.label]`
  - 当前 `physical_ymin/physical_ymax` 仍然直接等于像素坐标

也就是说，后续如果你要：

- 引入真实业务标签映射
- 按 `img_physical` 或其它规则换算物理坐标

第一修改点基本就在这里。

## 12. 协议层测试入口

位置：`tests/service_protocol_test.cpp`

主要验证：

- `ParseSubmitRequest(...)`
- `ParseInputRequest(...)`
- `Base64Decode(...)`
- `BuildResultJson(...)`
- `BuildTaskReplyJson(...)`
- `BuildTaskStateJson(...)`

建议：

- 如果你要改 JSON 结构，先改这里的测试，再改实现
- 这是后续继续演进协议最稳的入口

## 13. 建议排查顺序

以后遇到问题，建议按这个顺序查：

1. 看 `main.cpp` 是否启动了正确模式
2. 看 `RunUdpReceiver(...)` 判断接收端问题还是 worker 问题
3. 看路径校验函数判断共享目录问题
4. 看 `RunWorkerLoop(...)` 判断队列问题
5. 看 `ProcessTask(...)` 判断单任务执行问题
6. 看 `BuildDefects(...)` 和 `BuildResultJson(...)` 判断结果内容问题

## 14. 一句话记忆

- `main.cpp`：从哪进
- `udp_service.cpp`：流程怎么走
- `service_protocol.cpp`：数据怎么变
- `service_protocol_test.cpp`：协议有没有写对
