# 与上游 AstaPS 的差异

上游：[`MeChen618/AstaPS`](https://github.com/MeChen618/AstaPS)，7.1.0，Java，GPL-3.0。

上游 README 明确说明：**服务端不带客户端补丁**，要自己配 `hk4e-patch-universal` 或 Fiddler / mitmproxy。
本项目在上游基础上补了客户端那一侧，并改了登录链路，其余游戏逻辑一行没动。

## 服务端

登录链路是和上游差得最多的地方。上游的实现假设客户端发来的账号密码可以直接 RSA 解密，
而国服客户端用的私钥上游没有，那套代码在国服上永远抛 `CREDENTIAL_DECRYPTION_FAILED`。

| 文件 | 上游行为 | 本项目改动 | 原因 |
|---|---|---|---|
| `auth/MaPassportAuthenticator.java` | 解密失败直接返回错误；`verify` token 不匹配直接拒绝 | 解密失败回落（密文→固定账号，短明文→原样用）；`verify` 采纳客户端送来的 token | 没有那把私钥，只能回落；否则永远登不进 |
| `server/http/objects/VerifySTokenRequestJson.java` | 只认 `stoken` 字段 | 增加 `token` 对象与 `sessionToken()` | 客户端的请求体是 `{"mid":..,"token":{"token_type":1,"token":"v2_.."}}`，上游取到 null |
| `server/http/objects/VerifySTokenResponseJson.java` | 只有 `token_type` / `token` | 增加 `expires_in`、`account`、`refresh_token` | 客户端校验 `expires_in`，缺了判 token 无效，无限重试后报"登录状态失效" |
| `server/http/dispatch/AuthenticationHandler.java` | 没有 `/account/ma-cn-session/app/*` 路由 | 新增 `verify`、`exchange` 两条路由 + `maSessionExchange` 处理器 | 上游把 verify 兜底成 `{"retcode":0,"data":{}}`，exchange 返回空包，客户端换 token 失败 |

一句话：**上游没跑过国服客户端，登录链路是照国际服写的。** 换国际服客户端后其实上游原版也能登，
本项目的回落逻辑对国际服是兼容的（明文账号走原路径）。

## 客户端补丁

基底是 `KitsukiDev/hk4e-patch-universal`（上游 README 推荐的同一个），改动如下。

| 文件 | 改动 |
|---|---|
| `src/modules/http.rs` | URL 改写映射增加 `device-fp/getExtList`；`PASSTHROUGH_HOSTS` 增加 `public-data-api.mihoyo.com`（设备指纹走真服务）和 `sdk-static.mihoyo.com`（SDK 引导配置） |
| `src/modules/ccp_blocker.rs` | DNS 查询日志、按域名解析到 `0.0.0.0` 的 `block` 列表 |
| `src/lib.rs` | 可执行文件名放行 `GenshinImpact.exe`（上游只认 `YuanShen.exe`） |
| 日志 | 改为写文件，不再开隐形控制台（原版看不到任何输出） |

## 新增（上游完全没有）

| 模块 | 目录 | 作用 |
|---|---|---|
| 作弊插件 | `server/plugin/` | `/cheat` 命令 + `127.0.0.1:8890` HTTP 通道 + SDK 路由覆盖（`SdkRouter`） |
| 游戏内菜单 | `client/overlay/` | D3D11 Present 钩子 + ImGui，`INSERT` 呼出，六个分类 |
| 本地 TLS 前置 | `client/tools/mitm_sdk.py` | SDK 绕过 WinHTTP 走自己的 TLS，只能靠本地 443 前置截获 |
| 帧率定位工具 | `client/tools/fpsfind.py` | 静态扫 `GenshinImpact.exe` 找帧率全局变量，只读对比定位 |
| 体检脚本 | `scripts/` | 预检、自检、登录链测试、稳定性浸泡 |

## 菜单的能力边界

作弊是**服务端权威**的：菜单把开关写到 `127.0.0.1:8890`，插件翻译成服务端 GM 命令执行。
伤害倍率、掉落、时间、天气、刷物品这些都在服务端生效，不是客户端假象。

代价是必须让插件跑起来，且 8890 端口不能被占。

## 没做的事

- **帧率解锁没做进菜单。** 把 `genshin-fps-unlock` 的注入逻辑搬进菜单 DLL 后，进程内扫描
  il2cpp 段会触发客户端保护直接退出（实测三次）。改用它的独立程序，见
  [troubleshooting.md](troubleshooting.md#帧率)。
- **国服客户端没救活。** 详见 [troubleshooting.md](troubleshooting.md#国服客户端为什么崩)。
