# Geocaching 目录服务与互通工具

`directory_service.py` 是正在开发的持久目录服务，使用原生 Reticulum/LXMF
收发 Geocaching v1 请求。它提供 capabilities、publish、query、get、sync，
并从通过能力检查的公开目录自动拉取、验签和保存对象。没有 HTTP 业务查询接口。

`interop_peer.py` 仍是独立的、有容量限制的测试工具。它的 `directory` 模式只提供
内存中的样本，不应作为持久服务部署。

## 启动

使用 Python 虚拟环境安装固定依赖：

```sh
python -m venv .venv
# 激活虚拟环境后执行：
python -m pip install -r tools/geocaching/interop-requirements.txt
python tools/geocaching/directory_service.py --state /private/geocaching --rns-config /private/reticulum --name "Trail Mate directory"
```

Windows 上可以直接调用 `.venv\Scripts\python.exe`。`--rns-config` 指向**已有**
Reticulum 配置目录，其中必须存在 `config`；服务不生成或覆盖用户的网络配置。
连接公网、无线或已有传输节点由该配置决定。`--state` 中包含身份私钥、LXMF 状态、
SQLite 数据库及操作系统进程锁，应只允许运行服务的账户访问，不要提交到 Git。
同一状态目录只能运行一个服务进程。身份文件损坏时启动失败，不自动换身份。

正常运行持续到 SIGINT/SIGTERM。测试可使用 `--run-seconds` 设置运行期限，
以及 `--startup-delay 0..60` 固定首次 announce 的延迟。正式默认首次随机延迟
0..60 秒，周期为 6 小时 ±20%，内容更新 announce 至少间隔 30 分钟。

## 当前实现

- SQLite WAL / FULL 同步模式；签名对象、公开索引、变化日志和发布结果一起提交。
  落盘失败不发送成功响应。相同请求重放原结果；同 ID 不同内容不覆盖原请求。
- 查询快照和同步游标持久化；分页期间的新发布不会移动已捕获的同步水位。
  重启保留目录身份、epoch、对象、快照、请求结果和复制进度。
- 版本冲突冻结该 ID 的正常查询，保留并传播签名证明；精确历史版本仍可读取。
- 自动发现校验 discovery 和 LXMF delivery 的同身份绑定。复制先查能力，按精确
  hash 下载并独立验签；完整页落盘后才推进游标。未知结果重试保留原请求字节，
  重启不跳过未完成对象。错误 hash、无效签名或违规页面进入来源隔离。
- 首条直连消息先于发送方身份公告到达时，有界保存原始 LXMF 消息，等待身份
  最多 60 秒。取得身份后重新验证完整签名；未知身份或无效签名不执行请求。
- 最多 256 个同伴、一个主动复制请求、32 项入站队列、32 个在途出站消息。
  同伴超时按 1 分钟、5 分钟、30 分钟、6 小时退避；正常轮询间隔 6 小时。
  单轮复制应用字节预算 256 KiB，未完成页保留到后续轮次。
- 默认最多 100,000 个对象、1,000,000 条请求结果、256 个快照，每快照最多
  100,000 项。请求结果保留 30 天；分页快照 7 天，最终同步检查点 30 天。
  对象和日志目前不自动清理，容量满时拒绝新增，不删除已承诺保留的数据。

这些是本机功能实现及测试边界，**不是公网部署和长期运行验收已经完成的声明**。
仍需完善来源配额、磁盘总量控制与长期回收、低带宽复制策略、坏来源恢复与多来源
取证，以及真实部署的运维指标。浏览器 Worker、原始包 WSS 桥接和网页地图已实现，
并通过 [GitHub Pages](https://vicliu624.github.io/trail-mate/geocaching/) 验证公开测试点
查询、作者签名及 GPX 下载；浏览器内运行 RNS/LXMF，没有用 HTTP 替代业务通信。
当前 WSS 使用开发机上的临时隧道，尚不属于常驻部署。服务配置见
[部署说明](deploy/README.md)，设备与公网证据边界见
[公网网页与设备调度验收](../../docs/locales/zh-Hans/geocaching-lxmf/26-公网网页与设备调度验收.md)。

## 不编译固件的验证

在安装上述依赖的环境中，从仓库根目录运行：

```sh
python -m unittest discover -s tools/geocaching -p "test_directory_*.py" -v
python tools/geocaching/test_directory_network.py acceptance --work .codex-build/directory-network-run
python tools/geocaching/test_directory_replication_network.py acceptance --origin .codex-build/directory-network-run --work .codex-build/directory-replication-run
```

每次使用新的 `--work` 目录，避免覆盖先前证据。第二个网络测试读取第一个测试的
已发布对象，并使用原目录身份重新启动来源服务，不复制来源私钥到目标目录。
所有测试接口仅绑定 `127.0.0.1`；进程有明确运行上限并由脚本关闭。Windows
子进程使用隐藏窗口。日志和测试身份保存在各自工作目录。

存储/调度测试使用真实 Ed25519 签名和 SQLite，包括请求回执写入失败回滚、
分页快照、冲突证明、增量水位、中途重启、错误版本隔离及请求超时重试。
第一项网络测试由独立发布进程、目录进程和重启后的全新读取身份验证；第二项
验证目录自动复制，并在来源离线、目标重启后由新读取端取回同一签名对象。

## 2026-09-24 本机验证记录

- 存储与复制调度 10 项通过；身份晚到及无效签名拒绝 2 项通过。
- 独立发布端 → 持久目录 → 重启后新读取端通过：v1 发布、相同请求重放、
  query/get、v2 更新、历史 v1 读取、sync 基线与空增量。
  日志位于 `.codex-build/directory-network-20260924-a/`。
- 两个目录通过原生 announce 自动发现并以 LXMF 复制 v2；关闭来源、重启副本后，
  全新读取端仍能查询并获取同一签名原文。日志位于
  `.codex-build/directory-replication-20260924-c/`。
- 以上是本机 TCP 接口上的原生 RNS/LXMF 多进程测试，没有使用 HTTP 替代业务通信，
  没有进行本轮固件编译、刷写、真实无线链路或公网部署验收。
