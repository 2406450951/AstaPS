# 原神 7.1 私服 + 游戏内作弊菜单

**版本 1.8.1**

基于 [AstaPS](https://github.com/MeChen618/AstaPS)（Grasscutter 的 7.1.0 分支）搭的一套 Windows 单机环境，
外加一个自己写的游戏内菜单。作弊在服务端执行，客户端只负责发指令。

> 研究和个人存档用途，与米哈游 / HoYoverse 无关，不可商用。

## 1.8.1 修正

- 飞行改成**低频跳跃式**：800ms 一步、一步 6~8 米。上一版每 120ms 调一次 `teleport`，
  而 `teleport` 内部是 `transferPlayerToScene`，高频调用直接把客户端打进加载页白屏
- 试过"服务端广播 EntityMove 让角色自己走"，客户端无视（玩家角色是客户端权威），所以只能靠 teleport

## 1.8.0 新增

- **飞行 / 穿墙（传送式）**：勾上后服务端每 120ms 沿角色当前朝向传送一小步，速度 2~40 m/s 可调，
  另有上升/下降/前后切换。teleport 是服务端权威操作、不经过碰撞检测，所以能穿墙、上房顶、
  飞到正常去不了的位置
- 灵魂出窍（真·自由镜头）**没做**：探测发现 PC 端相机位置不是内存里的稳定 float 三元组
  （每帧由角色位置+视角四元数+距离现算，放在临时缓冲里），要做必须走指针扫描找相机组件，
  属于独立工程。飞行/穿墙是当前可交付的替代方案

## 1.7.1

- 移动速度界面改成诚实的开关：只保留能生效的「+30%（数据内置值）」。
  数据显示 MoveSpeed_lv2_1 只有一个 modifier、值固定 0.3，客户端认为该 modifier 已存在，
  不接受服务端改值，所以自定义倍数这条暂时走不通

## 1.7.0 新增

- **菜单打开时不再误操作角色**：hook 了游戏窗口的 WndProc，菜单打开期间鼠标左/右/中键、滚轮消息
  全部吞掉，只给 ImGui。点按钮不会再让角色攻击
- **每项作弊都能绑快捷键**：设置页列出 11 项（无敌/无摔落/自动拾取/无限体力/自动回血/杀意光环/
  冻结时间/实体数据流/怪物吸附/无限大招/技能可用），点「绑定」后按任意键即可，Esc 取消，
  也可以「清除」。绑完按一下开、再按一下关，快捷键写进 gs71_menu.cfg 持久保存
- GM 页的按钮不参与快捷键（按你的要求）

## 1.6.0 修正

- **移动速度**改用游戏原生的 buff 通道：先挂上 BuffExcel 里的「移速增加药品」
  （serverBuffId=170201，ability MoveSpeed_lv2_1，数据上就是 +30%），客户端认识这套数据；
  再用同一个 ability 名把 Animator_MoveSpeedRatio 覆盖成用户要的倍数。关加速时移除该 buff
- **自动拾取**支持采集点（日落果、树莓这类 GadgetGatherObject），验证通过

## 1.5.0 修正

- **自动拾取**：之前只处理怪物掉落的 EntityItem，而地图上的日落果/树莓这类是
  GadgetGatherObject（采集点），所以站在日落果旁边也没反应。现在对范围内的采集点直接调它自己的
  onInteract，走的是正常采集流程（拿物品、播采集表现、处理刷新）
- **移动速度**：客户端不认自造的 ability 名，改用数据里真实存在的
  LevelEntity_EffigyChallenge_MoveSpeed：先把 ability 挂到角色上，再发
  Animator_MoveSpeedRatio 属性

## 1.4.0

- GM 页重做：不再是一个命令输入框，而是把服务端 59 个命令**按用途做成控件**
  - 物品：give（物品 ID + 数量）、原石/摩拉/纠缠之缘 一键、清空背包/武器/圣遗物、重置商店限购
  - 角色：avatar add/set（ID + 等级）、team add、prop 设属性、满血、切换元素
  - 世界：spawn 刷怪（ID/等级/数量）、kill all、回七天神像、进副本、天气五档
  - 进度：quest 完成、成就全领、每日任务
  - 管理：全服公告、说话、踢人/封禁/解封、热重载
  - 其余 29 个命令（barrier / coop / cutscene / debug / entity / group / info / language /
    lookup / name / namecard / npc / permission / player / position / proto / setSceneTag /
    sgv / sound / support / teapot / trackmat / trial / windy / dps / help / account /
    battlepass / openstate）各一个按钮，点一下直接执行
- 移动速度：Animator_MoveSpeedRatio modifier，1.1x~4x

## 1.3.0 新增

- **移动速度**：给客户端发一个带 `Animator_MoveSpeedRatio` 的 modifier（属性名取自
  `resources/BinOutput` 的技能数据），1.1x~4x 可调。改的是服务端下发的属性，不碰客户端内存
- **GM 命令行**：菜单 GM 页多了一个输入框，直接敲服务端命令（不带斜杠），比如 `give 201 1000`、
  `unlockall`、`waypoints`、`clear weapons`；旁边还有几个常用按钮
- 服务端一共 60 个 GM 命令，游戏里输 `/help` 能看到全部

## 1.2.0 修正

- 自动拾取：补上 `ItemAddHintNotify`，客户端终于会显示 +物品；`/snap` 增加 `loot 扫描数 拾取数`
- 怪物吸附：`MotionInfo` 补全 rot / speed / state，`EntityMoveInfo` 补 sceneTime / reliableSeq，
  否则 7.1 客户端直接忽略这个移动包；`/snap` 增加 `mob 累计拉取数`
- 服务端日志会打 `auto-loot: N item(s), scanned M`

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

日常用 `scripts/启动.bat` 一条命令走完全部五步；只想起后台用 `scripts/只起服务端.bat`。

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

## 直接拿来用（不编译）

仓库只放源码。要用现成的二进制，需要下面四个文件，放到对应位置即可：

| 文件 | 放到哪 | 说明 |
|---|---|---|
| `Astrolabe.dll` | `<客户端目录>` | 菜单本体（由 `client/overlay` 构建，原名 `Gs71Menu.dll`） |
| `hk4e-patch.dll` | `<客户端目录>` | 客户端补丁（由 `client/patch` 构建，原名 `ext.dll`） |
| `CheatMenu.jar` | `server/plugins/` | 作弊插件（由 `server/plugin` 构建） |
| `resources/` | `server/resources/` | 7.1.0 资源包，1.6 GB |

前三个都是构建产物、不入库。作者打包好的版本见 Releases。

## 用之前要知道的

- 客户端必须是 **7.1.0 国际服**，国服会崩（见 [docs/troubleshooting.md](docs/troubleshooting.md)）。
- 本机 **443 端口**要给 TLS 前置用，被占用会起不来。
- 账号密码随便输都能进同一个本地账号（国服那把私钥拿不到，只能回落固定账号）。
- 天气只改气候（晴/阴/雨/雷/雪），不会触发蒙德城的暴风雨剧情；改成天气区域会让客户端崩。
- 帧率靠外置工具，不在菜单里，原因见 troubleshooting。

## 配置

| 文件 | 作用 | 常改项 |
|---|---|---|
| `server/config.json` | 服务端主配置 | `http.bindPort`（默认 8088）、`game.bindPort`（22101/UDP）、`account.autoCreate`、`game.gameOptions.newAccountIntro.enabled` |
| `hk4e-patch.cfg`（可选，放客户端目录） | 客户端补丁行为 | `target`（服务端地址）、`modules`（启用模块）、`dnslog`（打印 DNS 解析）、`block`（屏蔽域名） |
| `gs71_menu.cfg`（自动生成，放客户端目录） | 菜单开关与参数 | 作弊项默认全关，首次运行自动生成，改动即时写回 |
| `client/tools/mitm_sdk.py` | TLS 前置 | `LISTEN_PORT`（443）、`UPSTREAM`（转发目标） |

放在系统里、不随仓库走的三处：hosts 重定向、自签根证书、443 端口占用，详见
[docs/deploy.md](docs/deploy.md) 第 4 节。

## 菜单功能

| 分类 | 内容 |
|---|---|
| 玩家 | 无敌、无摔落伤害、无限体力、自动回血、伤害倍率（1~1000×）、UID/场景/坐标/血量 |
| 世界 | 自动拾取+半径、怪物吸附+半径、无限大招、技能可用、杀意光环、清场、补满血、冻结时间、改时间、天气、刷怪、给物品 |
| 视觉 | 实体数据流、雷达环、附近实体列表、水印、坐标 HUD、HUD 缩放 |
| 传送 | 坐标传送、地图标点传送、最近宝箱、场景切换、书签 |
| GM | 解锁地图点/传送点、清空背包、刷摩拉原石、副本传送 |
| 设置 | 呼出键、主题色、连接状态、延迟、最近动作 |

## 文档

- [docs/deploy.md](docs/deploy.md) — 从零部署：环境、资源包、系统级改动、编译、验证
- [docs/differences.md](docs/differences.md) — 与上游 AstaPS 的差异（逐文件）
- [docs/troubleshooting.md](docs/troubleshooting.md) — 常见故障、国服崩溃原因、已知限制

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
