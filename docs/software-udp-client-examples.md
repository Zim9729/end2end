# 软件侧 UDP 客户端示例

本文档给出三种常见客户端示例，帮助软件侧快速联调：

- C++/Qt 风格
- C#/.NET 风格
- Python 风格

示例目标：

- 向 Orin `192.168.145.100:9000` 发送 UDP `submit`
- 在本机 `192.168.145.1:9001` 接收 `accepted` 和 `done`
- 根据 `result_relpath` 去共享目录读取结果文件

## 1. 通用提交 JSON

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

## 2. C++ / Qt 示例

```cpp
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUdpSocket>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QUdpSocket socket;
    const quint16 localPort = 9001;
    socket.bind(QHostAddress("192.168.145.1"), localPort);

    QJsonObject submit;
    submit["cmd"] = "submit";
    submit["protocol_version"] = "1.0";
    submit["task_id"] = "8c8ea8f0-d1bc-4b38-a812-7eec537794b5";
    submit["request_relpath"] = "requests/2026/03/25/00001_1023.553_183701383.json";
    submit["result_relpath"] = "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json";
    submit["reply_ip"] = "192.168.145.1";
    submit["reply_port"] = 9001;

    const QByteArray payload = QJsonDocument(submit).toJson(QJsonDocument::Compact);
    socket.writeDatagram(payload, QHostAddress("192.168.145.100"), 9000);

    QObject::connect(&socket, &QUdpSocket::readyRead, [&socket]() {
        while (socket.hasPendingDatagrams()) {
            QByteArray buffer;
            buffer.resize(int(socket.pendingDatagramSize()));
            QHostAddress sender;
            quint16 senderPort = 0;
            socket.readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);

            const QJsonDocument doc = QJsonDocument::fromJson(buffer);
            const QJsonObject obj = doc.object();
            qDebug() << "recv from" << sender.toString() << senderPort << obj;

            const QString status = obj.value("status").toString();
            const QString resultRelpath = obj.value("result_relpath").toString();
            if (status == "success") {
                const QString resultFile = "Z:/rail_share/" + resultRelpath;
                qDebug() << "read result from" << resultFile;
            }
        }
    });

    return app.exec();
}
```

## 3. C# / .NET 示例

```csharp
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

class Program
{
    static async Task Main()
    {
        using var udp = new UdpClient(new IPEndPoint(IPAddress.Parse("192.168.145.1"), 9001));

        var submit = new
        {
            cmd = "submit",
            protocol_version = "1.0",
            task_id = "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
            request_relpath = "requests/2026/03/25/00001_1023.553_183701383.json",
            result_relpath = "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json",
            reply_ip = "192.168.145.1",
            reply_port = 9001
        };

        var json = JsonSerializer.Serialize(submit);
        var bytes = Encoding.UTF8.GetBytes(json);
        await udp.SendAsync(bytes, bytes.Length, "192.168.145.100", 9000);

        while (true)
        {
            UdpReceiveResult recv = await udp.ReceiveAsync();
            string text = Encoding.UTF8.GetString(recv.Buffer);
            Console.WriteLine($"recv: {text}");

            using JsonDocument doc = JsonDocument.Parse(text);
            string status = doc.RootElement.GetProperty("status").GetString() ?? "";
            string resultRelpath = doc.RootElement.TryGetProperty("result_relpath", out var rel) ? rel.GetString() ?? "" : "";

            if (status == "success")
            {
                string resultFile = Path.Combine(@"Z:\rail_share", resultRelpath.Replace('/', Path.DirectorySeparatorChar));
                Console.WriteLine($"read result from: {resultFile}");
                break;
            }
            if (status == "failed")
            {
                string error = doc.RootElement.TryGetProperty("error", out var err) ? err.GetString() ?? "" : "";
                Console.WriteLine($"task failed: {error}");
                break;
            }
        }
    }
}
```

## 4. Python 示例

```python
import json
import os
import socket

LOCAL_IP = "192.168.145.1"
LOCAL_PORT = 9001
ORIN_IP = "192.168.145.100"
ORIN_PORT = 9000
SHARE_ROOT = r"Z:\rail_share"

submit = {
    "cmd": "submit",
    "protocol_version": "1.0",
    "task_id": "8c8ea8f0-d1bc-4b38-a812-7eec537794b5",
    "request_relpath": "requests/2026/03/25/00001_1023.553_183701383.json",
    "result_relpath": "results/2026/03/25/8c8ea8f0-d1bc-4b38-a812-7eec537794b5_result.json",
    "reply_ip": LOCAL_IP,
    "reply_port": LOCAL_PORT,
}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LOCAL_IP, LOCAL_PORT))
sock.settimeout(30)

sock.sendto(json.dumps(submit, ensure_ascii=False).encode("utf-8"), (ORIN_IP, ORIN_PORT))

while True:
    data, addr = sock.recvfrom(65535)
    msg = json.loads(data.decode("utf-8"))
    print("recv from", addr, msg)

    status = msg.get("status", "")
    result_relpath = msg.get("result_relpath", "")

    if status == "success":
        result_file = os.path.join(SHARE_ROOT, *result_relpath.split('/'))
        print("read result from:", result_file)
        break
    if status == "failed":
        print("task failed:", msg.get("error", ""))
        break
```

## 5. 软件侧建议处理逻辑

- 发送 `submit` 后先等 `accepted`
- 收到 `accepted` 后不要重复提交相同 `task_id`
- 继续等待 `done`
- 若 `status=success`，按 `result_relpath` 读取结果文件
- 若 `status=failed`，记录 `error`
- 若短时间未收到任何回包，可重发相同 `task_id`

## 6. 注意事项

- `task_id` 必须在同一次任务重试中保持不变
- `request_relpath` 和 `result_relpath` 必须是相对路径
- UDP 中不要传图片或大 JSON
- 大数据只走共享目录
- Windows 端读取结果时，需要把 `/` 转为本地路径分隔符
