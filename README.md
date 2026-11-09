# 原神 7.1 私服 + 游戏内作弊菜单

**版本 1.9.5**

基于 [AstaPS](https://github.com/MeChen618/AstaPS)（Grasscutter 的 7.1.0 分支）搭的一套 Windows 单机环境，
外加一个自己写的游戏内菜单。作弊在服务端执行，客户端只负责发指令。

> 研究和个人存档用途，与米哈游 / HoYoverse 无关，不可商用。

## 1.9.5 改进（加速入口与神像自动定位）

### 加速开关挪到「玩家」页顶部

之前「移动加速」在「战斗辅助」下面，要滚动才能看到。现在提到生存分区下面、No Clip 之前：

```
Player · 玩家
☐ 无敌    ☑ 无限体力    ☐ 无摔落    ☑ 自动回血
─────────────────────────
移动加速（食物 buff +40%）      ← 就在这
☐ 开启移动加速
─────────────────────────
No Clip · 自由移动
```

顺带把一直未更新的句子换掉：旧文案写的是“客户端只认数据里的 0.3”（已废弃的
`Animator_MoveSpeedRatio` 路径），现在显示“直接挂食物移速 buff 170201（MoveSpeed_lv2_1），实测 +25~40%”。

不开菜单也能开：`GET /set?speed=1&r=1.4`（开）、`GET /set?speed=0`（关）。

### `c=sots` 自动找最近神像

`c=sots` 不再把点位写死在 4。现在默认遍历当前场景所有点位，用与生产代码一致的神像判定
（`getMaxSpringVolume() > 0` / 神像 gadgetId / 点位类型含 `KDEHKECBDBO`）按距离取最近的，
解锁 + 发解锁包 + 触发神像区域进入。

实例：站在蒙德某座神像旁输 `c=sots` → `sots scene=3 point=7 ... unlocked=true`，重登后出现「F 七天神像」。

## 1.9.4 修正（无限大招 / 移速 buff / 神像解锁）

### 无限大招：两个独立 bug

1. **能量不涨**：旧代码发的是 `PacketAvatarFightPropUpdateNotify`（战斗属性），客户端不认。
   改成 `EnergyManager.refillTeamEnergy(..., PropChangeReason_PROP_CHANGE_ENERGY_BALL, true)`。
2. **放完就没了**：服务端在大招释放后会**扣能量**。
   开启无限大招时调 `EnergyManager.setEnergyUsage(false)`（不扣），关闭时恢复 `true`。
3. **大招 CD**：`resetSkillCd` 原来只清当前出战角色，现在**遍历整个出战队伍**逐个发
   `AvatarSkillInfoNotify`（`passCdTime=0` + `fullCdTimeList=[0]`），切人后也没 CD。

实测：`c=energy` 显示四个角色 `usage=false`，连按 Q 可连续开大招。

### 移速：改成真的食物 buff

扫全部 buff 数据只有两个移速 buff：

```
170201  ab=MoveSpeed_lv2_1      mo=SERVER_MoveSpeed_Modifier_lv2_1
701018  ab=MoveSpeed_Dungeon    mo=SERVER_MoveSpeed_Dungeon_1
```

旧实现走的 `Animator_MoveSpeedRatio` + `AbilityMetaModifierChange` 客户端不认（实测 1.0×）。
现在直接 `addBuff(170201)`，**实测 +25~40%（基线 8.2m/1.5s → 10.6m/1.5s）**，每 240 秒续一次。

### 新增 GM/调试端点

| 命令 | 作用 |
|---|---|
| `c=sots&point=N` | 解锁就近七天神像点位（并发解锁包 + 触发神像区域进入） |
| `c=buff&id=N&dur=S` | 直接给出战角色挂 buff |
| `c=buffscan&match=XXX` | 按 ability/modifier 名搜 buff，用来找移速/攻速类 buff id |
| `c=energy` | dump 全队能量与 `EnergyManager` 的 `usage` 开关 |

实例：站在神像旁输 `c=sots` → `sots scene=3 point=4 … unlocked=true`，重登后游戏里出现「F 七天神像」交互。

## 1.9.3 新增（飞行稳定性 + 可绑定快捷键）

**修复：关飞行卡死 / 闪退**

`Astrolabe.log` 里的症状：

```
fly: mode -> 0
VEH exception=0xC0000005 at Astrolabe.dll+0x1772A      <- 崩在自己的 restoreRbState
服务端: 客户端已断开连接
```

两个原因：

1. 恢复前又调了 `findClassByName`（扫 20 万个类，约 2 秒）→ **卡住**
2. 直接写刚体指针没验证，对象池回收后指针失效 → **访问违例→闪退→掉线**

修法：

- 关飞行时**先抬升 1.5m** 再还原刚体（避免卡在墙/地里被物理强行拉扯）
- 所有刚体指针先 `VirtualQuery + ReadProcessMemory` 验证，死对象直接跳过
- 恢复时不再扫全类
- 飞行速度上限 **100 m/s**（菜单滑条 / `flyspeed=` / `[` `]`）；每帧位移上限 2.0 m
- 坐标越界或帧长 >0.15s（卡顿）时**自动关飞行**

实测：40 / 80 / 100 三档连开关，每次 `lifted 5 body(s)` → `restored on 5 body(s)`，0 秒完成，无崩溃。

**新增：所有功能快捷键可自定义**

菜单 → **设置** → 「作弊快捷键」表：

- 每行 = 功能名 + 当前键 + [绑定] + [清除]
- 点绑定 → 按任意键 → 立即生效并写入 `gs71_menu.cfg`（`Esc` 取消）
- 默认：NoClip=F1（备用 V）、速度飞行=F2、元素视野=F3、ESP=F4、自动奔跑=F5、跳过过场=F6、隐藏UI=F7、无敌=F8、光环=F9、时间冻结=F10、拾取=F11、体力=F12、速度+/-、[、]
- **菜单开关固定 INSERT**，不可改
- 同一个键不能给两个功能：绑新键时会清掉占用它的其他功能

**新增：无限技能（蓉力兼容）**

不再每 500ms 刷 CD（那样按住 E 蓉力永远蓄不完），改成：
**按 E 那一下清一次 CD**。实测日志 `act skillclear`。

## 1.9.2 修正（功能全失效 + 间歇崩溃）

**症状 A**：在线、菜单能开，但 NoClip / 飞行 / ESP 全都没反应，`Astrolabe.log` 里 `rbpos9: rb=0`。

原因：`VCHumanoidMove::.ctor` 的实例捕获钩子之前挂在调试开关
`ctor_auto.on` 上，那个文件不在就不装钩子 → `g_vchmInst[]` 为空 →
所有依赖实例列表的功能失效（纯服务端的 god/loot 照常，所以看起来像部分失效）。

修法：

- `installVchmCtorCapture()` 改成**无条件安装**（它是功能依赖，不是探针）
- 加兜底：`applyRbPosTick()` 发现 `g_vchmInstCount <= 0` 时，每 1.5 秒用
  `findClassByName + collectInstances` 重新填表

**症状 B**：修完 A 之后又闪退一次（还是 1.9.1 那个 `ActorUtils::.cctor` 崩溃）。

原因：**上一局会话在服务端没断**，`/snap` 仍然返回 `online 1`，
新客户端刚启动、世界还没加载完，`serverInWorld()` 就被这个"别人的 online"骗过去了。

修法：门控加启动宽限（崩溃点固定在启动后 ≈ 13 秒）：

```cpp
static bool serverInWorld() {
    if (g_bootTick == 0) g_bootTick = GetTickCount();
    if (GetTickCount() - g_bootTick < 20000) return false;   // 启动后 20 秒内一律不动
    Snapshot s = bridge::get();
    return s.valid && s.online;
}
```

代价：启动后最多多等 20 秒才能开菜单（进世界通常就超过了，体感几乎不变）。

## 1.9.1 修正（启动崩溃）

**症状**：客户端启动十几秒后直接消失（进不到登录界面），`Astrolabe.log` 里是：

```
VEH exception=0xC0000005 at GenshinImpact.exe+0x1FC75F0
   [7] GenshinImpact.exe+0x13ABFFF3     <- MoleMole.ActorUtils::.cctor + 0x13
```

**原因**：overlay 在**登录/加载阶段**就调用了 IL2CPP
（`Camera.get_main` / `GetAvatarPos` / ImGui 初始化），而游戏自己此刻正在初始化 `ActorUtils` 静态构造，
两边撞在一起就崩在 cctor+0x13。时间上就是“启动后 ~12 秒”。

**修法**：所有 IL2CPP 调用都等到**服务端报 online**（人已经在世界里）之后才开始：

```cpp
static bool serverInWorld() { Snapshot s = bridge::get(); return s.valid && s.online; }

void mainThreadTick() {
    if (!serverInWorld()) return;        // 没进世界就一律不碰 IL2CPP
    ...
}

// Present 钩子里
const bool inWorld = serverInWorld();
if (inWorld && !g_imguiReady && !failed) initImGui(swap);
if (inWorld && g_imguiReady) { /* 渲染 */ }
```

副作用：登录界面不再显示菜单（本来也没东西可看），进世界后一切照旧。

**排查提示**：启动后几秒内就消失 → 看 `Astrolabe.log` 的 `VEH exception`；
卡在“网络错误 4201” → 是服务端/443 没起，与 overlay 无关。

## 1.9.0 新增（重点）

### NoClip 真·自由移动（客户端写位置，不再靠 teleport）

把上一版“服务端往角色方向 teleport”换成了**客户端直接写位置**：

1. `Rigidbody.set_isKinematic(true)`（先把刚体变成运动学刚体，否则物理会把位置拉回去）
2. `Rigidbody.set_detectCollisions(false)`（关碰撞，穿墙）
3. 每帧 `Rigidbody.MovePositionDelta(delta)`，**delta 用镜像局部空间**（x/z 取反）
4. 顺手清零 `set_velocity`，关闭时自动恢复 `isKinematic(false)` + `detectCollisions(true)`

实测：`pos 2239.12 209.99 -934.18 → 2242.47 213.57 -939.91`（W+空格 3 秒），不掉血，
关闭时日志 `rigidbody state restored on 5 body(s)`。

> 顺带说明：之前试过的 `Transform::SetPosition`（按值/按指针两种 ABI）都会崩，
> 不要再走那条路。

### 元素视野（走游戏自己的 UI 开关）

不伪造状态，直接按游戏的 UI 逻辑点亮：
`MonoInLevelMainPage.get_elemViewButton() → UnityEngine.UI.Toggle.set_isOn(true)`。
开/关画面平均像素差 87~105（实测）。

### 快捷键全部绑定 + 焦点门控（不再是全局快捷键）

| 键 | 功能 | 键 | 功能 |
|---|---|---|---|
| F1 / V | NoClip 真·自由移动 | F7 | 隐藏 UI |
| F2 | 速度覆写飞行 | F8 | 无敌 |
| F3 | 元素视野 | F9 | 杀戮光环 |
| F4 | ESP 透视 | F10 | 时间冻结 |
| F5 | 自动奔跑 | F11 | 自动拾取 |
| F6 | 跳过过场 | F12 | 无限体力 |
| `[` / `]` | 飞行速度 -/+ | INSERT | 开关菜单 |

**之前所有按键用 `GetAsyncKeyState` 轮询，是全局的**（在别的窗口按一下也会触发）。
现在统一走一个门控：

```cpp
bool gs71InputFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (g_hwnd && fg == g_hwnd) return true;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
#define GetAsyncKeyState(vk) (gs71InputFocused() ? ::GetAsyncKeyState(vk) : 0)
```

entry.cpp 里 55 处轮询点被这个宏一次接管；菜单里的快捷键表另外显式判焦点。
实测：游戏前台按 F8 → `god 0→1`；切到记事本按 F8 → 无动作。

### ESP 分类 + 神瞳识别

- 实体流分类：怪 `M` / 宝箱 `C` / 机关 `K` / 采集 `I` / **神瞳 `O`**
- 查实了神瞳的真实 id（`gadscan` 证明神瞳不是 gadget，`itemgd` 查出 item→gadgetId）：
  - `100001 → 70540003`（风神瞳）
  - `100002 → 70540021`（岩神瞳）
- 按 id 段传送：`c=gadgettp&idmin=70540000&idmax=70540030`

### 工具箱

- 类名解析：`klass + 0x18` → 类名字符串
- **方法表：`klass + 0x20` → `MethodInfo*[]`，`MethodInfo + 0x0` = 函数地址**（用 LuaManager 三个已知 RVA 验证 3/3）
- `mdump`（按类名/TypeInfo 打方法表）、`gadscan` / `itemscan` / `itemgd`

### 修复

- `Gs71FlyMode()` 原来钳制 `0..3`，把 NoClip 的 `9` 吞掉了 —— 菜单里那个“真·自由移动”复选框之前实际没生效
- 菜单里“冻结敌人”其实绑的是服务端 `timeFreeze`（冻结时间），已改名并如实标注

### 未完成（已停）

笨蛋敌人 / 冻结敌人 / 无 CD —— 共同前置是客户端实体+组件枚举，
7.1 类名全混淆（`MonsterAI`/`AIController`/`VCMonster` 在 ida 表与运行时类名表均 0 命中），
实体又是对象池复用导致类差分无效。当前用 F8 无敌 + F9 光环 + `skillready/burst` 代替。

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
