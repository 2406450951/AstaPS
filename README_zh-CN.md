# AstaPS

[English](README.md) · 简体中文 · [繁體中文](README_zh-TW.md)

一个基于 Grasscutter 的《原神》**7.1.0** 私人服务器。

> 这是一个研究与保存性质的项目，与 HoYoverse / miHoYo 没有任何从属、背书或关联，亦不作商业用途。

如果你能修复问题，请帮帮我。

## 这是什么

- **内容跟得上版本。** 怪物与机关的生成数据对齐 7.1.0，深境螺旋轮换、秘境、圣遗物商店、战令等运营内容都在。
- **撑得住出问题的那天。** 写库拆成四个有界线程池，塞满时施加背压而不是丢掉玩家的存档；某个世界在 tick 中抛异常，不再让其他所有人的世界一起停摆。
- **出问题时看得见。** 状态输出会定时打印 CPU、内存、GC 以及每个线程池的队列深度，`/api/status` 也通过 HTTP 提供同一组数据。
- **全英文。** 源码、注释、提交信息、命令输出皆然。

## 需求

| | |
|---|---|
| Java | 编译需要 **JDK 21**，运行需要 **Java 21**。AstaPS 使用了虚拟线程等 Java 21 API。 |
| MongoDB | Community Server，必须在服务器启动前先跑起来。 |
| 游戏客户端 | 原神 7.1.0。官方客户端会校验 region 签名，要连私服需要另打客户端补丁，例如 [hk4e-patch-universal](https://github.com/MeChen618/patch)。AstaPS 本身不附带补丁。 |
| 资源文件 | 7.1.0 的资源包，解压到服务器目录下的 `resources/`。如果没有资源文件，可以通过[该链接](https://github.com/MeChen618/AstaPS-Resource)下载。 |

## 编译

编译前先确认两个 Java 命令都指向 21：

```
java -version
javac -version
```

然后执行：

```
./gradlew jar -PskipHandbook=1
```

`grasscutter-7.1.0.jar` 会生成在项目根目录。去掉 `-PskipHandbook=1` 可一并编译游戏内的手册；该步骤需要 NodeJS，没有就会失败。

Windows 上用 `.\gradlew.bat`，或运行 `gradlew-jar.bat`。

### 更快的本地编译

Gradle Wrapper 使用 **Gradle 9.8.1**。`gradle.properties` 默认开启构建缓存和配置缓存；如果协议数据和源码没有变化，生成结果和编译结果可以在多次构建及不同 Git 工作目录间复用。

仅编译、不打包 JAR：

```sh
./gradlew classes -PskipHandbook=1
```

本地测试时可以跳过 JAR 的 ZIP 压缩来缩短打包时间：

```sh
./gradlew jar -PskipHandbook=1 -PfastJar
```

Windows 下将 `./gradlew` 换为 `.\gradlew.bat`。两种方式都在项目根目录生成 `grasscutter-7.1.0.jar`。快速打包不会改变 JAR 内的文件，但体积较大（CI 测试中约 230 MB，普通压缩版约 85 MB）；发布时建议使用默认的压缩打包。

## 运行

1. 启动 MongoDB。
2. 把 7.1.0 资源包放进 `resources/`。
3. 先运行一次 jar。它会生成 `config.json`，若缺少关键内容则退出。
4. 再启动一次。默认情况下，调度服务器监听 `8088`，游戏服务器监听 `22101`。
5. 把客户端指向调度服务器。用 Fiddler、mitmproxy 之类的代理可以做到，客户端补丁也行。

### 账号

没有注册页面。账号通过以下任一方式创建：

- **从控制台。** `account create <username> [password] [@UID]`（密码与 UID 均可选，新账号仅使用配置中的默认权限，不自动获得 `*` 管理员权限）。
- **在登录时。** 用一个没人占用的名字登录即注册该账号。开启 `account.useIntegrationPassword` 时，在用户名框中填 `name&&password`，密码框留空——当你要同时代理大量客户端、又不想为每个都建用户时很方便。

设置的密码以 BCrypt 哈希存储。不指定密码时，账号暂不校验密码，可通过 `account resetpassword` 后续设置。控制台需要把 `server.game.enableConsole` 设为 `true`。

## 命令

游戏内命令以 `/` 开头，服务器控制台不需要。输入 `help` 查看命令列表，`help <命令> [子命令...]` 查看语法和别名。控制台支持 Tab 补全，参数错误时会显示对应语法。

玩家选择器支持 `@UID`、`username@` 和 `username@UID`。账号管理仅限服务器控制台。

```text
help teleport pos
give 202 --amount 3 @10001
```

详细说明见 [CLI 迁移文档](docs/cli-picocli-migration.md)；插件开发参阅 [命令 API v5](docs/plugin-command-api-v5.md)。

## TPS 射击（7.1）

至冬的第三人称射击模式可用：枪械与手雷会装备在角色普通武器旁的槽位，可在 TPS 秘境中瞄准并射击。相关命令需要 `player.tps` 和 `player.enterdungeon` 权限。

**1. 获取武器**

```
/tps give
/tps give 224001
/tps accessory
```

第一条命令发放全部八把 TPS 武器（224001–224008）；第二条只发放 224001；第三条解锁已拥有武器的配件。

**2. 游玩 TPS 秘境**

```
/dungeon 10955
/dungeon 10953
/dungeon 10960
```

每行都是一条独立命令。10955 是靶场，10953 与 10960–10964 是涌出的灰色原野各阶段。

进入后，你的队伍会被替换为 TPS 旅行者（与你使用的旅行者对应，等级 20），并带上你的 TPS 装备，首次则为 224001。在那里切换使用的武器会保留为你的装备配置。离开秘境后恢复你的队伍。

**3. 秘境之外**

任何角色都可以装备 TPS 武器，方便试玩：

```
/tps wear 224001 224004
/tps refill
```

第一条命令装备步枪和手雷（至多两把枪、一枚手雷），第二条补满弹药。

弹药处理仍部分处于实验阶段。服务端做了什么、`/tps ammo` 的开关，以及尚未定论的部分，见 [docs/tps/README.md](docs/tps/README.md)。

## 许可

以 **GNU General Public License v3.0** 发布。见 [`LICENSE`](LICENSE)。

`LICENSE-ClassGraph.txt` 不是本项目的许可。ClassGraph 是一个 MIT 许可的依赖，其编译后的类随 `grasscutter-7.1.0.jar` 一同分发，MIT 只要求其声明随附其中。

## 致谢

本服务器基于 **Grasscutter**。参考项目：**LunaGC**、**HunkyMeow**。

需求中链接的客户端补丁 [hk4e-patch-universal](https://github.com/MeChen618/patch) 是 [capyb2222/animegamepatch](https://github.com/capyb2222/animegamepatch) 的副本，由 **capyb2222** 维护，基于 [xeondev](https://git.xeondev.com/reversedrooms/hk4e-patch) 最初的 hk4e-patch 以及 [oureveryday](https://github.com/oureveryday/) 最初的 hk4e-patch-universal。它是独立项目，遵循其自身的 GPL-3.0 许可。

本仓库根目录的导入提交，按姓名列出了其承载作品的作者。

## 协议来源

协议定义来源于 [genshin-protocol](https://gitlab.com/kitkat-multiverse/genshin-protocol)。
