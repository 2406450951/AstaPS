# 原神 7.1 私服 + 游戏内作弊菜单

基于 [AstaPS](https://github.com/MeChen618/AstaPS)（Grasscutter 的 7.1.0 分支）搭的一套 Windows 单机环境，
外加一个自己写的游戏内菜单。服务端权威执行作弊，客户端只负责发指令。

> 研究和个人存档用途，与米哈游 / HoYoverse 无关，不可商用。

## Requirements

| 组件 | 版本 | 必需 | 说明 |
|---|---|---|---|
| 原神客户端 | **7.1.0 国际服** | 是 | 国服客户端进世界 40~85 秒后必崩，见 [docs/troubleshooting.md](docs/troubleshooting.md) |
| JDK | 21 | 是 | 服务端运行时与构建都用 21 |
| MongoDB | 7.x Community | 是 | 账号与存档 |
| Python | 3.11（带 tkinter） | 是 | 本地 TLS 前置、校验脚本 |
| 资源包 | 7.1.0 | 是 | 约 1.6 GB，从 [AstaPS-Resource](https://github.com/MeChen618/AstaPS-Resource) 下载，解压到 `server/resources/` |
| MSYS2 / mingw64 | gcc 16 及以上 | 否 | 只在要重新编译菜单 DLL 时需要 |
| Rust | cargo 1.99 及以上 | 否 | 只在要重新编译客户端补丁时需要 |
| .NET Desktop Runtime | 8 | 否 | 只有用外置帧率解锁工具时需要（它带自包含版可免装） |

## 快速开始

```powershell
# 1) 放资源包
#    下载 AstaPS-Resource 的 7.1.0，解压到 server/resources/

# 2) 起服务端
powershell -ExecutionPolicy Bypass -File server/start-server.ps1

# 3) 起本地 TLS 前置（截获 SDK 登录，转发给服务端）
powershell -ExecutionPolicy Bypass -File scripts/起TLS前置.ps1

# 4) 清客户端本地登录痕迹（否则会拿旧 token 反复自检）
powershell -ExecutionPolicy Bypass -File scripts/重置登录状态.ps1

# 5) 部署两个 DLL 到客户端目录（名字固定，游戏只认这两个）
Copy-Item <构建产物>\Gs71Menu.dll   "<客户端目录>\Astrolabe.dll"  -Force
Copy-Item <构建产物>\ext.dll        "<客户端目录>\hk4e-patch.dll" -Force
```

启动游戏 → 登录页随便输账号密码（勾协议）→ 进世界 → 按 `INSERT` 呼出菜单。

日常用 `scripts/启动.bat` 一条命令走完全部五步；它和 `scripts/只起服务端.bat` 的区别见
[docs/deploy.md](docs/deploy.md)。

## 目录结构

```
.
├── server/                     服务端
│   ├── config.json             主配置（端口、账号策略、游戏选项）
│   ├── start-server.ps1        起停脚本，自动拉起 MongoDB
│   └── plugin/                 作弊插件（Java，AstaPS 插件形式加载）
│       ├── build.ps1           编译成 server/plugins/CheatMenu.jar
│       └── src/cheatmenu/      CheatCommand / CheatApi / Bridge / SdkRouter …
│
├── client/                     客户端一侧
│   ├── overlay/                游戏内菜单（C++ / ImGui / MinHook，D3D11 Present 钩子）
│   ├── patch/                  客户端补丁（Rust，hk4e-patch-universal 改写版）
│   └── tools/                  TLS 前置脚本、帧率变量定位、PE 分析等小工具
│
├── scripts/                    启动、自检、登录链测试、稳定性测试
└── docs/                       部署、差异、故障排查
```

仓库不含二进制产物、资源包、第三方 vendored 源码，构建方式见 [docs/deploy.md](docs/deploy.md)。

## 配置

| 文件 | 作用 | 常改项 |
|---|---|---|
| `server/config.json` | 服务端主配置 | `http.bindPort`（默认 8088）、`game.bindPort`（22101/UDP）、`account.autoCreate`、`game.gameOptions.newAccountIntro.enabled` |
| `hk4e-patch.cfg`（可选，放客户端目录） | 客户端补丁行为 | `target`（服务端地址）、`modules`（启用模块）、`dnslog`（打印 DNS 解析）、`block`（屏蔽域名） |
| `gs71_menu.cfg`（自动生成，放客户端目录） | 菜单开关与参数 | 作弊项默认全关，首次运行自动生成，改动即时写回 |
| `client/tools/mitm_sdk.py` | TLS 前置 | `LISTEN_PORT`（443）、`UPSTREAM`（转发目标） |

放在系统里、不随仓库走的三处：hosts 重定向、自签根证书、443 端口占用，详见
[docs/deploy.md](docs/deploy.md) 第 4 节。

## 文档

- [docs/deploy.md](docs/deploy.md) — 从零部署：环境、资源包、系统级改动、编译、验证
- [docs/differences.md](docs/differences.md) — 与上游 AstaPS 的差异（逐文件）
- [docs/troubleshooting.md](docs/troubleshooting.md) — 常见故障、国服客户端崩溃原因、已知限制

## 许可

服务端基于 Grasscutter / AstaPS，遵循 **GPL-3.0**（见 [LICENSE](LICENSE)）。
客户端补丁来自 [hk4e-patch-universal](https://github.com/KitsukiDev/hk4e-patch-universal)，
原作者 xeondev、oureveryday、capyb2222，保留其原始许可。

## 致谢

- [Grasscutter](https://github.com/Grasscutters/Grasscutter) — 服务端原型
- [AstaPS](https://github.com/MeChen618/AstaPS) — 7.1.0 分支，本项目的服务端基础
- [hk4e-patch-universal](https://github.com/KitsukiDev/hk4e-patch-universal) — 客户端补丁基础
- [genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock) — 帧率上限变量的定位方法
- [ImGui](https://github.com/ocornut/imgui)、[MinHook](https://github.com/TsudaKageyu/minhook) — 菜单界面与钩子
