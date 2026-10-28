# 部署

从零开始，到进游戏能作弊。全程用仓库内相对路径，客户端目录用 `<客户端目录>` 代指。

## 1. 装环境

### 1.1 JDK 21

必需。7.1 服务端用了 Java 21 的虚拟线程，17 跑不起来。

```powershell
java -version    # 必须是 21.x
$env:JAVA_HOME  # 设好，start-server.ps1 会用它找 java
```

### 1.2 MongoDB 7.x

必需。`start-server.ps1` 会自己找 `mongod.exe`（按 `PATH`、`MONGODB_HOME`、常见安装路径依次找），
找不到就报错并退出。也可以先自己起一个监听 27017 的实例。

### 1.3 Python 3.11

必需（要 tkinter）。只有 TLS 前置和几个校验脚本用，不参与游戏逻辑。

```powershell
python -c "import tkinter, ssl; print('ok')"
```

### 1.4 编译环境（可选）

只有要改代码才装：

- MSYS2 + mingw64（gcc 16+）：编译 `client/overlay`
- Rust（cargo 1.99+）：编译 `client/patch`

不装也能用：直接用别人编译好的 `Gs71Menu.dll` / `ext.dll` 部署即可。

## 2. 资源包

```powershell
# 从 https://github.com/MeChen618/AstaPS-Resource 下 7.1.0
# 解压后目录里应该直接看到 TextMap/、ExcelBinOutput/、BinOutput/ 等
server/resources/
├── TextMap/
├── ExcelBinOutput/
├── BinOutput/
└── ...（约 4 万个文件，1.6 GB）
```

放错层级是新手最常见的错误：`server/resources/TextMap/` 对，`server/resources/resources/TextMap/` 错。

## 3. 客户端

用 **7.1.0 国际服**（HoYoPlay 安装）。`config.ini` 里应该能看到：

```ini
game_version=7.1.0
cps=hoyoverse
```

国服客户端不要用，原因见 [troubleshooting.md](troubleshooting.md#国服客户端为什么崩)。

客户端目录里最终要有这两个文件（名字固定，游戏按这两个名字加载）：

| 文件名 | 来源 | 作用 |
|---|---|---|
| `Astrolabe.dll` | `client/overlay` 的构建产物 `Gs71Menu.dll` 改名 | 游戏启动时会加载它，它再拉起补丁 DLL 并装菜单 |
| `hk4e-patch.dll` | `client/patch` 的构建产物 `ext.dll` 改名 | 把游戏的请求重定向到本地服务端 |

## 4. 系统级改动

这三处不在仓库里，换机器必须重做。

### 4.1 hosts

```
127.0.0.1 passport-api.hoyoverse.com
127.0.0.1 hk4e-sdk-os.hoyoverse.com
127.0.0.1 sdk-os-static.hoyoverse.com
127.0.0.1 api-os-takumi.hoyoverse.com
127.0.0.1 hk4e-api-os.hoyoverse.com
127.0.0.1 dispatchosglobal.yuanshen.com
```

（用国服客户端再加 `passport-api.mihoyo.com` 等，但国服不建议用。）

### 4.2 自签根证书

TLS 前置需要一张能签 `*.hoyoverse.com` 的证书，并且系统要信任它。

```powershell
# 生成一次即可；sdk_cert.pem / sdk_key.pem 由 gs71_ca.pem 签出，不进仓库
certutil -addstore -f Root gs71_ca.pem
```

不装根证书，客户端会直接拒绝 TLS 握手，登录页会停在加载状态。

### 4.3 443 端口

TLS 前置占 443。机器上有 IIS、某些 VPN、Docker 映射占用 443 的话先腾出来：

```powershell
netstat -ano | findstr :443
```

## 5. 编译

### 5.1 服务端插件

```powershell
powershell -ExecutionPolicy Bypass -File server/plugin/build.ps1
# 产物：server/plugins/CheatMenu.jar
# 注意：jar 被 java 占用时无法覆盖，先停服务端
```

### 5.2 菜单 DLL

```bash
# 先拉第三方库（仓库不带）
git clone https://github.com/ocornut/imgui -b docking client/overlay/vendor/imgui-docking
git clone https://github.com/TsudaKageyu/minhook  client/overlay/vendor/minhook-master

bash client/overlay/build.sh
# 产物：client/overlay/build/Gs71Menu.dll
```

### 5.3 客户端补丁

```bash
cd client/patch
cargo build --release
# 产物：client/patch/target/release/ext.dll
```

### 5.4 只改服务端几个 Java 类

不必全量构建 `grasscutter.jar`，单编再塞进去：

```powershell
javac -nowarn -cp grasscutter.jar -d _patchout <源文件.java>
# 用 python 的 zipfile 覆盖进去；jar uf 会因为重复条目 META-INF/LICENSE 报错
```

## 6. 启动

```powershell
# 服务端
powershell -ExecutionPolicy Bypass -File server/start-server.ps1
#   起 MongoDB（若没在跑）→ 起 grasscutter → 监听 8088/TCP、22101/UDP
#   参数：-Status 看状态，-Stop 停，-Restart 重启

# TLS 前置
powershell -ExecutionPolicy Bypass -File scripts/起TLS前置.ps1
#   监听 443，把 HTTPS 请求转成明文 HTTP 打到 127.0.0.1:8088

# 清客户端登录痕迹
powershell -ExecutionPolicy Bypass -File scripts/重置登录状态.ps1
#   删注册表里的 __LastUid / player_sessionid 和 SDKCaches\HoYoPass 下的 token
#   不清的话客户端会拿旧 token 反复自检，界面报"登录状态失效"且不给输入框
```

然后启动游戏。登录页随便输账号密码，勾协议，进世界。

### 一键入口

| 脚本 | 行为 |
|---|---|
| `scripts/启动.bat` | 预检 → 服务端 → TLS 前置 → 清登录痕迹 → 启动客户端 |
| `scripts/只起服务端.bat` | 同上但**不启动客户端**，适合客户端由别的工具拉起 |
| `scripts/停止.bat` | 停服务端和 TLS 前置 |

## 7. 验证

```powershell
# 服务端在跑、版本对
Invoke-WebRequest http://127.0.0.1:8088/status/server -UseBasicParsing | % Content
# {"retcode":0,"status":{"playerCount":1,...,"version":"7.1.0"}}

# 菜单与服务端的通道通
Invoke-WebRequest http://127.0.0.1:8890/ping -UseBasicParsing | % Content
# ok

# 世界内状态
Invoke-WebRequest http://127.0.0.1:8890/snap -UseBasicParsing | % Content
# online 1 / uid / scene 3 / pos ... / hp ...
```

或者直接跑 `scripts/自检.bat`，15 项全 PASS 就说明链路没问题；`scripts/登录链测试.bat`
不开游戏也能验证 7 步登录链。

## 8. 换电脑的检查清单

- [ ] JDK 21、MongoDB、Python 3.11 装好
- [ ] `server/resources/` 放对层级
- [ ] 客户端是 7.1.0 国际服
- [ ] `Astrolabe.dll`、`hk4e-patch.dll` 两个名字正确
- [ ] hosts 六行写完
- [ ] 根证书装进 `LocalMachine\Root`
- [ ] 443 没被占
- [ ] 服务端 + TLS 前置都起来了
