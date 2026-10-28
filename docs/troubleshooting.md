# 故障排查

## 登录相关

### 一直报"登录状态失效"，不给输入框

客户端缓存了旧 token，拿它反复调 `verify` 自检，失败就停在那个界面。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/重置登录状态.ps1
```

它删两处东西：注册表里的 `__LastUid___h2153286551` / `unity.player_sessionid_h1351336811`，
以及 `<客户端目录>\<X>_Data\SDKCaches\HoYoPass\` 下的 token 文件。

### 登录页卡在加载，输完没反应

按顺序查：

1. TLS 前置没起 → `netstat -ano | findstr :443`
2. 根证书没信任 → `certutil -store Root | findstr GS71`
3. hosts 没写 → `ping passport-api.hoyoverse.com` 应解析到 `127.0.0.1`
4. 服务端 8088 没起 → `netstat -ano | findstr :8088`

### 弹 `The client is damaged, please reinstall the client. Error Code:(2,0,4)`

这个弹窗是 SDK 用 `NtRaiseHardError` 弹的（窗口归属 csrss，不是 MessageBox），
**它是登录失败的收尾动作，不是崩溃原因**。登录流程一旦通了它就不再出现。

如果登录已经通了还反复弹，说明 `verify` 或 `exchange` 的应答结构不对，
看 [differences.md](differences.md) 里那两个 response 类。

## 国服客户端为什么崩

国服 7.1 客户端能登录、能进世界、作弊也生效，但进世界 40~85 秒后必定崩：

```
0xC0000005  MHYPBase.dll+0x17112B   movzx eax, byte ptr [rdi + rbx]
栈：+0x9804C → +0x98734 → +0x99294 → +0xAAE9D → +0x176DEE
```

`mhypbase.dll` 里那段是 VM 派发（读跳转表的字节再算偏移）。这个地址崩说明 `rdi`（表基址）
或 `rbx`（索引）非法，也就是 VM 状态已经坏了——国服 SDK 判定自己被篡改后走的那条分支。

试过的手段和结果：

| 手段 | 结果 |
|---|---|
| 跳过出错指令（rax 归零 + RIP+4） | 换到 `MHYPBase+0x17110F2` 继续崩，打地鼠 |
| 堵遥测域名（`sdk-log-upload` 等） | 无效，崩溃时机不变 |
| 校验文件完整性（三份清单 2743+4858+176 项） | 全齐，不是文件问题 |
| 模拟键盘输入绕过网页登录 | VK / Unicode / Ctrl+V / 扫描码全试过，到不了登录网页 |

**结论：换国际服客户端，这个问题直接消失。** 国际服 `mhypbase.dll` 没有这条判定分支。

## 菜单相关

### 菜单呼不出来

- 客户端目录必须有 `Astrolabe.dll` 和 `hk4e-patch.dll` 这两个**固定文件名**，改名不生效
- 游戏启动早期就会加载 `Astrolabe.dll`，如果它是启动后才放进去的，重启游戏
- 换过构建的话确认 DLL 是 64 位（`dumpbin /headers` 看 machine 是 x64）

### 菜单能开，开关点了没反应

菜单和服务端插件之间的通道断了：

```powershell
Invoke-WebRequest http://127.0.0.1:8890/ping -UseBasicParsing | % Content
# 期望 ok
```

不通就查插件有没有加载（`server/plugins/CheatMenu.jar` 存在？服务端日志里有没有插件加载记录）。

### 一进世界就崩，崩溃日志里有 `Astrolabe.dll`

这是我们自己代码的锅，不是游戏。历史上有两次：

- `sscanf_s` 用 `%c` 没给长度参数，踩坏堆 → 改成 `sscanf` + `%1c`
- 帧率扫描遍历全模块内存，踩到未提交页 → 已从覆盖层移除

复现时看 `<客户端目录>\Astrolabe.log`，里面会打模块+偏移。

## 帧率

游戏内设置只有 30 / 60 两档。要更高得改内存里的帧率全局变量。

**本项目的菜单不做这件事。** 把 [genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock)
的注入逻辑搬进菜单 DLL 后，进程内扫描 il2cpp 段会触发客户端保护，客户端直接退出（实测三次），
所以改成用它自己的独立程序：

1. 下它的 self-contained 版（自带 .NET，不用装运行时）
2. 首次运行会列出找到的游戏安装 —— **注意它可能列到国服**，要 Browse 手动选
   `<客户端目录>\GenshinImpact.exe`
3. FPS 设 144，勾 Start Game Automatically
4. **由它启动游戏**，不要先手动开客户端，否则它挂不上去

顺带说明：Akebi 没有改帧率的功能，`cheat-base/src/cheat-base/cheat/misc/Settings.cpp` 里只有
`Show FPS Indicator` / `Move FPS Indicator`（帧数显示）。144 帧来自上面那个独立工具。

## 服务端

### 起不来

```powershell
powershell -ExecutionPolicy Bypass -File server/start-server.ps1 -Status
```

看是哪一步挂：Mongo 没起、8088 被占、`resources/` 层级不对、JDK 不是 21。

### 起来了但客户端连不上

`22101` 是 UDP，防火墙可能只放行了 TCP。检查：

```powershell
Get-NetFirewallRule | Where-Object DisplayName -match 'grasscutter|java'
```

### 插件改了没生效

`grasscutter.jar` 或 `CheatMenu.jar` 被 java 进程占用时无法覆盖。先 `-Stop` 再编再起。
