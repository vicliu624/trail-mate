# 多入口部署

网页自动连接我们维护的 WSS 服务。服务器上的一个 Reticulum 实例同时连接多个
公共 TCP 入口，并承载持久寻宝目录。普通访客没有网络设置界面。

```text
浏览器 RNS/LXMF Worker
        │ WSS 原始 RNS 数据包
        ▼
TLS 反向代理 → packet_bridge.py → 本机 127.0.0.1:44242
                                      │
                           Reticulum transport + 寻宝目录
                             ├── 悉尼 TCP 入口
                             ├── ReticulumNet NL TCP 入口
                             └── RMAP TCP 入口
```

`reticulum/config` 已配置三个独立 TCPClientInterface 和一个只监听 loopback 的
TCPServerInterface。多个上游同时启用，由 Reticulum 管理接口、路径和重连；
桥接程序不把一个包自行广播到多个 TCP socket，不实现另一套选路或故障切换。
现有链路中断后仍可能需要重新发现路径及重试应用请求，不能承诺无缝切换。

部署时将 `reticulum/` 复制到服务器私有持久目录，以该目录作为服务的
`--rns-config`。例如将配置置于 `/srv/trail-mate/reticulum/config` 后，分别运行：

```sh
python tools/geocaching/directory_service.py --state /srv/trail-mate/directory --rns-config /srv/trail-mate/reticulum --name "Trail Mate public directory"
python tools/geocaching/packet_bridge.py --tcp-host 127.0.0.1 --tcp-port 44242 --port 8787 --origin https://vicliu624.github.io
```

用具备有效证书的 TLS 反向代理将自有域名的 WSS 请求转发至 `127.0.0.1:8787`，
保留 WebSocket 升级、二进制消息与 Origin。然后将真实 WSS URL 写入
`site/geocaching/network.json` 的 `endpoint`。若网站改用自定义域名，也要更新
桥接的 `--origin`。这些配置只由运营方修改。

默认上游为 `sydney.reticulum.au:4242`、`node.reticulumnet.nl:4242` 和
`rmap.world:4242`。按长期观测记录和公共服务说明筛选，已移除只有一次连通性
检查证据的裸 IP。依据与限制见[设备网络配置](../../../docs/reticulum_network_config.md)。
观测时长不是连续在线率；本机 DNS 经代理，TCP 连接成功也不能证明源站可用。
已通过临时接入验证公网寻宝目录发现和测试点读取，尚未验证公网运营方故障恢复或长期可用性。
多个上游不意味着存在多个寻宝目录，也不会消除单台桥接服务器的故障风险。
可在后续部署第二台桥接及目录副本后增加浏览器的自动备用连接。

截至 2026-09-24，GitHub Pages 已发布，`network.json` 已配置开发机临时 WSS
隧道及公开目录的 discovery seed。线上页面已查询到测试点、验证作者签名并下载
GPX；另一次未拦截地图请求的检查确认真实 OpenStreetMap 瓦片加载成功。
测试点位于 0,0，仅用于联调，没有实体宝藏。临时服务依赖开发机在线，隧道地址
可能失效；以下 systemd/Caddy 配置仍需部署到运营方的常驻服务器。

本机多接口恢复已有真实 RNS/LXMF 测试：浏览器完成列表查询后，关闭实际选中
上游的 TCP 转发端口，拒绝其重连，再从另一接口完成签名详情和 GPX 下载。
随后重启 WSS 桥接，原标签页无需刷新或设置即可重新查询。测试入口为：

```sh
node tests/site/geocaching-browser.mjs --multi-upstream --reconnect --work .codex-build/browser-recovery-run --python /path/to/venv/python
```

两个本机上游连接同一个目录，证明的是独立 TCP 接口失效后的恢复，不代表
两个独立公网运营方、不同地理位置或目录副本的故障验收已经完成。

## Linux 常驻服务

仓库附带 `trail-mate-directory.service`、`trail-mate-bridge.service`、
`bridge.env` 和 `Caddyfile`。以下布局适用于具有 systemd、Python 3.10+
和 Caddy 2 的 Linux 服务器。部署代码位于 `/opt/trail-mate`，由管理员拥有，
服务账户只能写 `/var/lib/trail-mate` 中的目录数据、身份及 Reticulum 状态。

验证边界：当前开发机仅完成文件审查与 `git diff --check`。本机 WSL 的
`systemd-analyze verify` 因 control group 权限错误未能初始化管理器，
未完成 unit 验证；本机无 Caddy 可执行文件，也尚未验证真实 TLS 配置。
以下服务器侧检查是正式部署前仍需执行的步骤，不是已通过的记录。

安装代码后，由管理员创建专用账户和虚拟环境：

```sh
sudo useradd --system --home-dir /var/lib/trail-mate --shell /usr/sbin/nologin trail-mate
sudo python3 -m venv /opt/trail-mate/.venv
sudo /opt/trail-mate/.venv/bin/pip install -r /opt/trail-mate/tools/geocaching/interop-requirements.txt -r /opt/trail-mate/tools/geocaching/bridge-requirements.txt
sudo install -d -o trail-mate -g trail-mate -m 0700 /var/lib/trail-mate /var/lib/trail-mate/reticulum
sudo install -d -m 0755 /etc/trail-mate
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/bridge.env /etc/trail-mate/bridge.env
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/trail-mate-directory.service /etc/systemd/system/
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/trail-mate-bridge.service /etc/systemd/system/
```

首次安装时复制 Reticulum 配置；升级时保留已有配置和整个状态目录：

```sh
sudo test -e /var/lib/trail-mate/reticulum/config || sudo install -o trail-mate -g trail-mate -m 0600 /opt/trail-mate/tools/geocaching/deploy/reticulum/config /var/lib/trail-mate/reticulum/config
sudo systemd-analyze verify /etc/systemd/system/trail-mate-directory.service /etc/systemd/system/trail-mate-bridge.service
```

将 `Caddyfile` 中的 `geocaching.example.org` 替换为真实域名，合入服务器的
`/etc/caddy/Caddyfile`，保留服务器已有站点。域名 DNS 必须指向该服务器，
允许外部访问 TCP 80/443；8787 和 44242 保持 loopback，不开放公网端口。
若网站使用自定义域名，先修改 `/etc/trail-mate/bridge.env` 中的精确 Origin。
Origin 不含 URL 路径：GitHub Pages 项目路径不应写入该值。

```sh
sudo caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
sudo systemctl daemon-reload
sudo systemctl enable --now trail-mate-directory.service trail-mate-bridge.service
sudo systemctl reload caddy
sudo journalctl -u trail-mate-directory.service -u trail-mate-bridge.service -n 100 --no-pager
```

这两项服务在异常退出后自动重启。目录重启期间已有桥接连接可能断开，
浏览器需要重新连接、发现路径并重试请求；systemd 的启动顺序不等于目录已就绪。
Caddy 负责证书和 WebSocket 转发。桥接进程重启也会断开现有连接。

先通过真实 HTTPS 网页验证 WSS 连接、目录发现、签名详情与 GPX 下载，
再将 `wss://真实域名/rns` 写入 `site/geocaching/network.json` 的 `endpoint` 并发布网站。
同时将该目录实际公告的 discovery destination 写入 `discoverySeeds`（最多三个
32 位十六进制字符串）；这里是发现目的地址，不是 LXMF delivery 地址。网页会
在连接和重连后请求这些目的的路径，仍验证发现公告和 delivery 的同身份绑定。
本仓库示例域名不是可用服务，不得直接作为生产 endpoint。
还需验证重启服务器后目录身份不变、已发布记录仍可查询，以及单个上游
中断后的恢复；这些验收尚未执行。

备份时先停止桥接和目录服务，再复制完整 `/var/lib/trail-mate`，完成后恢复服务。
不要只复制正在写入的 SQLite 主文件而遗漏 WAL。该目录含私钥，备份必须私有保存。
升级应用代码和虚拟环境时停止服务，保留状态目录、Origin 和 Caddy 配置；
回退代码前检查数据库格式兼容性。

协议配置参考：<https://reticulum.network/manual/interfaces.html>
