# 原神 7.1 私服 + 游戏内作弊菜单（AstaPS 7.1 分支）

**版本 1.9.0** ｜ 单机私服 ｜ 仅研究/个人存档用途

---

## ⚠️ 先看这个：这是**私服**，不是**官服**

这个仓库部署的是**跑在你自己电脑上的单机私服**（Grasscutter 的 7.1 分支 + 一个自写的客户端 overlay）。
客户端 exe 虽然装在 HoYoPlay 的官方目录里，但它连的是 `127.0.0.1`，**不是米哈游服务器**。

### 三条铁律

1. **不要用 HoYoPlay / 官方启动器启动游戏**。启动器会走官方 dispatch，直接把你送进官服。
   只能直接运行下面写的那个 `GenshinImpact.exe`，而且要**先起服务端 + TLS 前置**。
2. **TLS 前置（443）没起来，就不要开客户端**。SDK 登录请求会打到真的 `passport-api.mihoyo.com`，
   结果是官服登录页 / 连上官服。
3. **每次进游戏先确认 `online 1` 和游戏内水印**（见第 3 节验证），别凭感觉。

### 一眼分辨你在哪个服

| 检查点 | 官服 | 私服（本项目） |
|---|---|---|
| 登录界面 | 要米哈游账号密码 + 图形验证码 | 只有「点击进入」按钮 |
| 进游戏后左上角 | 没有水印 | `GS71 · 原神 7.1 · online` |
| 聊天框 | 正常的官方频道 | `Welcome to the Chiori test server` |
| `http://127.0.0.1:8890/snap` | 打不开 | 返回 `online 1` |
| 角色/背包数据 | 你官服的号 | 空号（uid 2，神/1级） |

> 如果你看到"要米哈游账号密码"，**立刻退出客户端**，按第 5 节排错处理，不要在官服里继续点。

---

## 1. 系统架构

```
GenshinImpact.exe（官方安装目录里的客户端，打了 patch）
   │  ① SDK 登录：HTTPS → passport-api.mihoyo.com:443
   ▼
mitm_sdk.py  TLS 前置（监听 443，本地 CA 签发，转发明文）
   │  ② 明文 HTTP
   ▼
Grasscutter 私服  127.0.0.1:8088（HTTP/SDK）  22101（游戏）  dispatch ws://127.0.0.1:1111
   │  ③ 读写存档
   ▼
MongoDB（.run/mongo）

作弊菜单（本项目的 overlay，注入在客户端进程里）
   │  ④ 只发 HTTP 指令
   ▼
CheatMenu 插件  127.0.0.1:8890（挂在私服进程里，作弊逻辑全部服务端执行）
```

要点：**作弊逻辑在服务端执行**（伤害/掉落/时间/传送都以服务端为准），客户端 overlay 只负责画菜单和发指令，
所以单人私服里的表现和 Akebi 那种"改客户端内存"不一样 —— 除了 NoClip（那个是真改客户端位置）。

---

## 2. 启动 / 停止

### 2.1 启动（顺序不能乱）

```powershell
# 1) 服务端 + MongoDB（幂等，重复调用只报状态）
powershell -ExecutionPolicy Bypass -File .\server\start-server.ps1

# 2) TLS 前置：监听 443 → 转发到 127.0.0.1:8088
powershell -ExecutionPolicy Bypass -File .\scripts\起TLS前置.ps1

# 3) 客户端：直接起 exe（不要去点 HoYoPlay）
& 'D:\Program Files\HoYoPlay\games\Genshin Impact game\GenshinImpact.exe'

# 4) 登录界面点「点击进入」
```

### 2.2 验证（每次都做）

```powershell
# 三个端口都在监听 = 链路通
Get-NetTCPConnection -LocalPort 443,8088,22101 -State Listen | Select-Object LocalPort

# 私服在线
(Invoke-WebRequest 'http://127.0.0.1:8890/snap' -UseBasicParsing).Content -split "`n" | Select-String 'online'
# 期望：online 1
```

游戏内再确认左上角有 `GS71 · 原神 7.1 · online` 水印。

### 2.3 停止

```powershell
powershell -ExecutionPolicy Bypass -File .\server\start-server.ps1 -Stop   # 连 mongo 一起停
Get-Process python | Where-Object { $_.Path -like '*python311*' } | Stop-Process -Force   # 停 443 前置
```

### 2.4 改了代码之后

| 改了什么 | 怎么生效 |
|---|---|
| `client/overlay/*.cpp` | `client/overlay/build.sh` → 把 `build/Gs71Menu.dll` 复制成客户端目录的 `Astrolabe.dll` → 重启客户端 |
| `server/plugin/src/**` | 先 `start-server.ps1 -Stop`（jar 被占用就编不了）→ `server/plugin/build.ps1` → `start-server.ps1` |
| `server/config.json` | `start-server.ps1 -Restart` |

---

## 3. 目前做了什么（已完成，都实测过）

### 3.1 客户端 overlay（`client/overlay`，注入成 `Astrolabe.dll`）

| 功能 | 状态 | 验证证据 |
|---|---|---|
| **NoClip 真·自由移动** | ✅ 实测 | `pos 2239.12 209.99 -934.18 → 2242.47 213.57 -939.91`（W+空格 3 秒），不掉血；关闭时 `rigidbody state restored on 5 body(s)` |
| 速度覆写飞行（旧方案，保留） | ✅ | 跟镜头方向平移 |
| 元素视野 | ✅ | 走游戏自己的 UI 开关，开/关画面平均像素差 87~105 |
| ESP 透视 | ✅ | 怪 / 宝箱 / 机关 / 采集 / **神瞳** 分类 + 连线 + 血条 + 宝箱指引箭头 |
| 自动奔跑 | ✅ | 沿镜头方向持续前进 |
| 跳过过场 / 对话加速 | ✅ | 钩 `CriwareMediaPlayer`，时间倍率 1~5x |
| 隐藏 UI / 去雾 / FOV / 帧率 | ✅ | — |
| 快捷键 + **焦点门控** | ✅ | 前台按 F8 → `god 0→1`；切到记事本按 F8 → 无动作 |
| 调试工具箱 | ✅ | 类名解析、方法表导出 `mdump`、实例扫描、堆扫类计数 |

### 3.2 服务端插件（`server/plugin`，`CheatMenu.jar`）

| 功能 | 状态 |
|---|---|
| 无敌 / 无摔落 / 无限体力 / 自动回血 | ✅ |
| 击杀光环、怪物吸附、无限大招、技能可用 | ✅ |
| 自动拾取（半径可调） | ✅ |
| 按 id 段传送：宝箱 `702x` / 采集 `705x·717x` / 机关 `7036x` / **神瞳 `70540003`(风) `70540021`(岩)** | ✅ |
| 自动交互（广域 onInteract，3 秒冷却）、自动解谜 | ✅ |
| GM 命令透传、场景切换、书签、时间/天气 | ✅ |
| 取证命令 `gadscan` / `itemscan` / `itemgd` / `gadids` | ✅ |

### 3.3 这一版（1.9.0）修掉的坑

- `Gs71FlyMode()` 原来钳制 `0..3`，把 NoClip 的 `9` 吞掉了 —— 菜单里"真·自由移动"复选框之前**实际没生效**
- 菜单里「冻结敌人」绑的其实是服务端 `timeFreeze`（冻结**时间**），已改名并如实标注
- 所有按键原来是 `GetAsyncKeyState` 全局轮询，**在别的窗口按也会触发**；现在统一走 `gs71InputFocused()` 门控

---

## 4. 剩余什么（未完成）

| 功能 | 卡在哪 | 现在用什么代替 |
|---|---|---|
| 笨蛋敌人 Dumb Enemies | 7.1 类名全混淆：`MonsterAI` / `AIController` / `VCMonster` 在 ida 表与运行时类名表 **0 命中**；实体走对象池复用，类实例差分看不出新类 | **F8 无敌**（怪照打但不掉血）+ **F9 杀戮光环** |
| 冻结敌人 Freeze Enemies | 需要客户端实体枚举；服务端只有时间冻结 | 无（F10 是冻结时间） |
| 无 CD NoCD | 需要客户端技能组件的 CD 字段 | `skillready=1` + `burst=1` |
| 相机距离缩放（Akebi 原版） | 缺 `SCameraModuleInitialize` 类名 | FOV 滑条（等效） |

---

## 5. 正在进行什么

**目标**：拿到客户端的**实体 + 组件枚举**能力 —— 这是上面 3 个未完成项的共同前置。

已经试过并排除的路：

1. **按名字找类**：ida 表 / 运行时类名表搜 `MonsterAI`、`AIController`、`VCMonster`，全部 0 命中
2. **`ActorUtils` 找遍历接口**：没有实体列表 API，只有按 `entityId` 查坐标的 `TryGetIceCreatureWorldPosition`
3. **堆扫类实例差分**（生怪前后对比哪些类实例变多）：工具能用，但输出全是 UI/场景类 —— 实体是**对象池复用**，计数不变
4. **按怪物坐标反查对象**：扫 417 MB 堆，坐标三元组找到 11 处，唯一带类名的 `HMBFLDJPNPA` 是 0x60 的池化小结构（**相邻对象**，不是实体）

下一步候选：扫 klasses 找 MoleMole 命名空间/乱码名的类、按字段特征筛；或者从 `TryGetIceCreatureWorldPosition` 反查 ActorManager。

> 这一块需要独立的逆向投入，已经**暂停**，不影响现有功能使用。

---

## 6. 快捷键（**只在游戏窗口前台生效**）

| 键 | 功能 | 键 | 功能 |
|---|---|---|---|
| F1 / V | NoClip 真·自由移动 | F7 | 隐藏 UI |
| F2 | 速度覆写飞行 | F8 | 无敌 |
| F3 | 元素视野 | F9 | 杀戮光环 |
| F4 | ESP 透视 | F10 | 时间冻结 |
| F5 | 自动奔跑 | F11 | 自动拾取 |
| F6 | 跳过过场 | F12 | 无限体力 |
| `[` / `]` | 飞行速度 -/+ | INSERT | 开关菜单 |

门控实现（`client/overlay/entry.cpp`）：

```cpp
bool gs71InputFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (g_hwnd && fg == g_hwnd) return true;              // 游戏窗口
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();                  // 本进程其它窗口
}
#define GetAsyncKeyState(vk) (gs71InputFocused() ? ::GetAsyncKeyState(vk) : 0)
```

entry.cpp 里 55 处轮询点被这个宏一次接管；菜单里的快捷键表另外显式判焦点，失焦时清空边沿状态。

---

## 7. 目录结构

```
AstaPS/
├─ client/
│  ├─ overlay/          # 游戏内菜单（C++/ImGui，D3D11 Present 钩子）→ 注入成 Astrolabe.dll
│  │  ├─ entry.cpp      # 主入口、钩子、NoClip/ESP/元素视野、工具箱
│  │  ├─ menu.cpp       # Akebi 式 UI、快捷键表、服务端状态同步
│  │  ├─ bridge.cpp/.h  # 与 127.0.0.1:8890 的 HTTP 桥
│  │  └─ build.sh       # msys2 g++ 构建
│  ├─ patch/            # Rust 写的 dispatch/RSA 补丁（hk4e-patch）
│  └─ tools/            # 诊断脚本
├─ server/
│  ├─ start-server.ps1  # 起停 Grasscutter + MongoDB（幂等）
│  ├─ config.json       # 私服配置（8088 / 22101 / dispatch 1111）
│  └─ plugin/           # CheatMenu 插件源码 + build.ps1
├─ scripts/             # 起TLS前置.ps1、启动.bat、自检.ps1、预检.ps1 等
├─ VERSION              # 1.9.0
└─ README.md            # 本文件（仓库里唯一的 .md）
```

---

## 8. 排错

| 症状 | 原因 | 处理 |
|---|---|---|
| **要米哈游账号密码 / 进的是官服** | 443 前置没起，或 443 被别的程序占了，或走了 HoYoPlay 启动器 | 退客户端 → `Get-NetTCPConnection -LocalPort 443 -State Listen` 看占用 → 起 `起TLS前置.ps1` → 再起客户端 |
| 卡白屏 / 一直加载 | 服务端没起或 jar 崩了 | 看 `server/server.log`；`start-server.ps1 -Status` |
| 登录后掉线 | TLS 前置中途退出 | 重新 `起TLS前置.ps1`，确认 443 在监听 |
| 菜单不显示 | overlay 没注入 / 版本不匹配 | 确认客户端目录 `Astrolabe.dll` 是新构建；INSERT 开菜单 |
| 传送后掉到未加载地形（HP 00/00） | 坐标在未加载区域 | `http://127.0.0.1:8890/cmd?c=tp&x=2236&y=212&z=-915` 回蒙德 |
| 插件改动不生效 | jar 被占用没重编 | `start-server.ps1 -Stop` → `server/plugin/build.ps1` → `start-server.ps1` |

> `scripts/部署到国际服.ps1` **与本项目无关**（那个是把补丁部署到国际服客户端），不要用它。

---

## 9. 技术附录（逆向得到的坐标/偏移，7.1）

```
类名           klass + 0x18
方法表         klass + 0x20  → MethodInfo*[]  → MethodInfo + 0x0 = 函数地址
VCHumanoidMove 0x500026f1fe0   rb +0x570   moveData +0x6d0   footTrans +0x700   fsm +0x698
Rigidbody      set_isKinematic 0x19C5E250   set_detectCollisions 0x19C5E520
               set_velocity 0x19C5E130      MovePositionDelta 0x19C5E650
ActorUtils     GetAvatarPos 0x13AAF060      GetCameraPos 0x13AB7B80
Camera         get_main 0x19BF6D90  set_fieldOfView 0x19BF5C10  Transform.get_forward 0x19C015D0
Criware        Update 0x12DA0D90   Skip 0x12DA06F0
UI             MonoInLevelMainPage.get_elemViewButton 0x121D66B0   Toggle.set_isOn 0x19D12D50
```

**NoClip 配方**（不要走 `Transform::SetPosition`，它两种 ABI 都会崩）：

1. `Rigidbody.set_isKinematic(true)`
2. `Rigidbody.set_detectCollisions(false)`
3. 每帧 `Rigidbody.MovePositionDelta(delta)`，**delta 用镜像局部空间（x/z 取反）**
4. 顺手 `set_velocity(0)`；关闭时恢复 `isKinematic(false)` + `detectCollisions(true)`

**神瞳 id**（`gadscan` 证明神瞳不是 gadget，`itemgd` 查出 item→gadgetId）：
`100001 → 70540003`（风）、`100002 → 70540021`（岩）。

**注意事项**

- 所有 IL2CPP 调用必须在主线程（`hkPresent → mainThreadTick`），从 worker 线程调会 AV
- 改 `entry.cpp` 不要用 PowerShell 的 `Set-Content`（会破坏编码），用按字节改
- `flyspeed` 建议 ≤10，太快会被服务端位置校验注意到

---

## 附：版本历史

### 1.9.0 新增（重点）

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

### 1.8.1 修正

- 飞行改成**低频跳跃式**：800ms 一步、一步 6~8 米。上一版每 120ms 调一次 `teleport`，
  而 `teleport` 内部是 `transferPlayerToScene`，高频调用直接把客户端打进加载页白屏
- 试过"服务端广播 EntityMove 让角色自己走"，客户端无视（玩家角色是客户端权威），所以只能靠 teleport

### 1.8.0 新增

- **飞行 / 穿墙（传送式）**：勾上后服务端每 120ms 沿角色当前朝向传送一小步，速度 2~40 m/s 可调，
  另有上升/下降/前后切换。teleport 是服务端权威操作、不经过碰撞检测，所以能穿墙、上房顶、
  飞到正常去不了的位置
- 灵魂出窍（真·自由镜头）**没做**：探测发现 PC 端相机位置不是内存里的稳定 float 三元组
  （每帧由角色位置+视角四元数+距离现算，放在临时缓冲里），要做必须走指针扫描找相机组件，
  属于独立工程。飞行/穿墙是当前可交付的替代方案

### 1.7.1

- 移动速度界面改成诚实的开关：只保留能生效的「+30%（数据内置值）」。
  数据显示 MoveSpeed_lv2_1 只有一个 modifier、值固定 0.3，客户端认为该 modifier 已存在，
  不接受服务端改值，所以自定义倍数这条暂时走不通

### 1.7.0 新增

- **菜单打开时不再误操作角色**：hook 了游戏窗口的 WndProc，菜单打开期间鼠标左/右/中键、滚轮消息
  全部吞掉，只给 ImGui。点按钮不会再让角色攻击
- **每项作弊都能绑快捷键**：设置页列出 11 项（无敌/无摔落/自动拾取/无限体力/自动回血/杀意光环/
  冻结时间/实体数据流/怪物吸附/无限大招/技能可用），点「绑定」后按任意键即可，Esc 取消，
  也可以「清除」。绑完按一下开、再按一下关，快捷键写进 gs71_menu.cfg 持久保存
- GM 页的按钮不参与快捷键（按你的要求）

### 1.6.0 修正

- **移动速度**改用游戏原生的 buff 通道：先挂上 BuffExcel 里的「移速增加药品」
  （serverBuffId=170201，ability MoveSpeed_lv2_1，数据上就是 +30%），客户端认识这套数据；
  再用同一个 ability 名把 Animator_MoveSpeedRatio 覆盖成用户要的倍数。关加速时移除该 buff
- **自动拾取**支持采集点（日落果、树莓这类 GadgetGatherObject），验证通过

### 1.5.0 修正

- **自动拾取**：之前只处理怪物掉落的 EntityItem，而地图上的日落果/树莓这类是
  GadgetGatherObject（采集点），所以站在日落果旁边也没反应。现在对范围内的采集点直接调它自己的
  onInteract，走的是正常采集流程（拿物品、播采集表现、处理刷新）
- **移动速度**：客户端不认自造的 ability 名，改用数据里真实存在的
  LevelEntity_EffigyChallenge_MoveSpeed：先把 ability 挂到角色上，再发
  Animator_MoveSpeedRatio 属性

### 1.4.0

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

### 1.3.0 新增

- **移动速度**：给客户端发一个带 `Animator_MoveSpeedRatio` 的 modifier（属性名取自
  `resources/BinOutput` 的技能数据），1.1x~4x 可调。改的是服务端下发的属性，不碰客户端内存
- **GM 命令行**：菜单 GM 页多了一个输入框，直接敲服务端命令（不带斜杠），比如 `give 201 1000`、
  `unlockall`、`waypoints`、`clear weapons`；旁边还有几个常用按钮
- 服务端一共 60 个 GM 命令，游戏里输 `/help` 能看到全部

### 1.2.0 修正

- 自动拾取：补上 `ItemAddHintNotify`，客户端终于会显示 +物品；`/snap` 增加 `loot 扫描数 拾取数`
- 怪物吸附：`MotionInfo` 补全 rot / speed / state，`EntityMoveInfo` 补 sceneTime / reliableSeq，
  否则 7.1 客户端直接忽略这个移动包；`/snap` 增加 `mob 累计拉取数`
- 服务端日志会打 `auto-loot: N item(s), scanned M`
