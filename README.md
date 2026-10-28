# 鍘熺 7.1 绉佹湇 + 娓告垙鍐呬綔寮婅彍鍗?
鍩轰簬 [AstaPS](https://github.com/MeChen618/AstaPS)锛圙rasscutter 鐨?7.1.0 鍒嗘敮锛夋惌鐨勪竴濂?Windows 鍗曟満鐜锛?澶栧姞涓€涓嚜宸卞啓鐨勬父鎴忓唴鑿滃崟銆傛湇鍔＄鏉冨▉鎵ц浣滃紛锛屽鎴风鍙礋璐ｅ彂鎸囦护銆?
> 鐮旂┒鍜屼釜浜哄瓨妗ｇ敤閫旓紝涓庣背鍝堟父 / HoYoverse 鏃犲叧锛屼笉鍙晢鐢ㄣ€?
## Requirements

| 缁勪欢 | 鐗堟湰 | 蹇呴渶 | 璇存槑 |
|---|---|---|---|
| 鍘熺瀹㈡埛绔?| **7.1.0 鍥介檯鏈?* | 鏄?| 鍥芥湇瀹㈡埛绔繘涓栫晫 40~85 绉掑悗蹇呭穿锛岃 [docs/troubleshooting.md](docs/troubleshooting.md) |
| JDK | 21 | 鏄?| 鏈嶅姟绔繍琛屾椂涓庢瀯寤洪兘鐢?21 |
| MongoDB | 7.x Community | 鏄?| 璐﹀彿涓庡瓨妗?|
| Python | 3.11锛堝甫 tkinter锛?| 鏄?| 鏈湴 TLS 鍓嶇疆銆佹牎楠岃剼鏈?|
| 璧勬簮鍖?| 7.1.0 | 鏄?| 绾?1.6 GB锛屼粠 [AstaPS-Resource](https://github.com/MeChen618/AstaPS-Resource) 涓嬭浇锛岃В鍘嬪埌 `server/resources/` |
| MSYS2 / mingw64 | gcc 16 鍙婁互涓?| 鍚?| 鍙湪瑕侀噸鏂扮紪璇戣彍鍗?DLL 鏃堕渶瑕?|
| Rust | cargo 1.99 鍙婁互涓?| 鍚?| 鍙湪瑕侀噸鏂扮紪璇戝鎴风琛ヤ竵鏃堕渶瑕?|
| .NET Desktop Runtime | 8 | 鍚?| 鍙湁鐢ㄥ缃抚鐜囪В閿佸伐鍏锋椂闇€瑕侊紙瀹冨甫鑷寘鍚増鍙厤瑁咃級 |

## 蹇€熷紑濮?
```powershell
# 1) 鏀捐祫婧愬寘
#    涓嬭浇 AstaPS-Resource 鐨?7.1.0锛岃В鍘嬪埌 server/resources/

# 2) 璧锋湇鍔＄
powershell -ExecutionPolicy Bypass -File server/start-server.ps1

# 3) 璧锋湰鍦?TLS 鍓嶇疆锛堟埅鑾?SDK 鐧诲綍锛岃浆鍙戠粰鏈嶅姟绔級
powershell -ExecutionPolicy Bypass -File scripts/璧稵LS鍓嶇疆.ps1

# 4) 娓呭鎴风鏈湴鐧诲綍鐥曡抗锛堝惁鍒欎細鎷挎棫 token 鍙嶅鑷锛?powershell -ExecutionPolicy Bypass -File scripts/閲嶇疆鐧诲綍鐘舵€?ps1

# 5) 閮ㄧ讲涓や釜 DLL 鍒板鎴风鐩綍锛堝悕瀛楀浐瀹氾紝娓告垙鍙杩欎袱涓級
Copy-Item <鏋勫缓浜х墿>\Gs71Menu.dll   "<瀹㈡埛绔洰褰?\Astrolabe.dll"  -Force
Copy-Item <鏋勫缓浜х墿>\ext.dll        "<瀹㈡埛绔洰褰?\hk4e-patch.dll" -Force
```

鍚姩娓告垙 鈫?鐧诲綍椤甸殢渚胯緭璐﹀彿瀵嗙爜锛堝嬀鍗忚锛夆啋 杩涗笘鐣?鈫?鎸?`INSERT` 鍛煎嚭鑿滃崟銆?
鏃ュ父鐢?`scripts/鍚姩.bat` 涓€鏉″懡浠よ蛋瀹屽叏閮ㄤ簲姝ワ紱瀹冨拰 `scripts/鍙捣鏈嶅姟绔?bat` 鐨勫尯鍒
[docs/deploy.md](docs/deploy.md)銆?
## 鐩綍缁撴瀯

```
.
鈹溾攢鈹€ server/                     鏈嶅姟绔?鈹?  鈹溾攢鈹€ config.json             涓婚厤缃紙绔彛銆佽处鍙风瓥鐣ャ€佹父鎴忛€夐」锛?鈹?  鈹溾攢鈹€ start-server.ps1        璧峰仠鑴氭湰锛岃嚜鍔ㄦ媺璧?MongoDB
鈹?  鈹斺攢鈹€ plugin/                 浣滃紛鎻掍欢锛圝ava锛孉staPS 鎻掍欢褰㈠紡鍔犺浇锛?鈹?      鈹溾攢鈹€ build.ps1           缂栬瘧鎴?server/plugins/CheatMenu.jar
鈹?      鈹斺攢鈹€ src/cheatmenu/      CheatCommand / CheatApi / Bridge / SdkRouter 鈥?鈹?鈹溾攢鈹€ client/                     瀹㈡埛绔竴渚?鈹?  鈹溾攢鈹€ overlay/                娓告垙鍐呰彍鍗曪紙C++ / ImGui / MinHook锛孌3D11 Present 閽╁瓙锛?鈹?  鈹溾攢鈹€ patch/                  瀹㈡埛绔ˉ涓侊紙Rust锛宧k4e-patch-universal 鏀瑰啓鐗堬級
鈹?  鈹斺攢鈹€ tools/                  TLS 鍓嶇疆鑴氭湰銆佸抚鐜囧彉閲忓畾浣嶃€丳E 鍒嗘瀽绛夊皬宸ュ叿
鈹?鈹溾攢鈹€ scripts/                    鍚姩銆佽嚜妫€銆佺櫥褰曢摼娴嬭瘯銆佺ǔ瀹氭€ф祴璇?鈹斺攢鈹€ docs/                       閮ㄧ讲銆佸樊寮傘€佹晠闅滄帓鏌?```

浠撳簱涓嶅惈浜岃繘鍒朵骇鐗┿€佽祫婧愬寘銆佺涓夋柟 vendored 婧愮爜锛屾瀯寤烘柟寮忚 [docs/deploy.md](docs/deploy.md)銆?
## 閰嶇疆

| 鏂囦欢 | 浣滅敤 | 甯告敼椤?|
|---|---|---|
| `server/config.json` | 鏈嶅姟绔富閰嶇疆 | `http.bindPort`锛堥粯璁?8088锛夈€乣game.bindPort`锛?2101/UDP锛夈€乣account.autoCreate`銆乣game.gameOptions.newAccountIntro.enabled` |
| `hk4e-patch.cfg`锛堝彲閫夛紝鏀惧鎴风鐩綍锛?| 瀹㈡埛绔ˉ涓佽涓?| `target`锛堟湇鍔＄鍦板潃锛夈€乣modules`锛堝惎鐢ㄦā鍧楋級銆乣dnslog`锛堟墦鍗?DNS 瑙ｆ瀽锛夈€乣block`锛堝睆钄藉煙鍚嶏級 |
| `gs71_menu.cfg`锛堣嚜鍔ㄧ敓鎴愶紝鏀惧鎴风鐩綍锛?| 鑿滃崟寮€鍏充笌鍙傛暟 | 浣滃紛椤归粯璁ゅ叏鍏筹紝棣栨杩愯鑷姩鐢熸垚锛屾敼鍔ㄥ嵆鏃跺啓鍥?|
| `client/tools/mitm_sdk.py` | TLS 鍓嶇疆 | `LISTEN_PORT`锛?43锛夈€乣UPSTREAM`锛堣浆鍙戠洰鏍囷級 |

鏀惧湪绯荤粺閲屻€佷笉闅忎粨搴撹蛋鐨勪笁澶勶細hosts 閲嶅畾鍚戙€佽嚜绛炬牴璇佷功銆?43 绔彛鍗犵敤锛岃瑙?[docs/deploy.md](docs/deploy.md) 绗?4 鑺傘€?
## 鏂囨。

- [docs/deploy.md](docs/deploy.md) 鈥?浠庨浂閮ㄧ讲锛氱幆澧冦€佽祫婧愬寘銆佺郴缁熺骇鏀瑰姩銆佺紪璇戙€侀獙璇?- [docs/differences.md](docs/differences.md) 鈥?涓庝笂娓?AstaPS 鐨勫樊寮傦紙閫愭枃浠讹級
- [docs/troubleshooting.md](docs/troubleshooting.md) 鈥?甯歌鏁呴殰銆佸浗鏈嶅鎴风宕╂簝鍘熷洜銆佸凡鐭ラ檺鍒?
## 璁稿彲

鏈嶅姟绔熀浜?Grasscutter / AstaPS锛岄伒寰?**GPL-3.0**锛堣 [LICENSE](LICENSE)锛夈€?瀹㈡埛绔ˉ涓佹潵鑷?[hk4e-patch-universal](https://github.com/KitsukiDev/hk4e-patch-universal)锛?鍘熶綔鑰?xeondev銆乷ureveryday銆乧apyb2222锛屼繚鐣欏叾鍘熷璁稿彲銆?
## 鑷磋阿

- [Grasscutter](https://github.com/Grasscutters/Grasscutter) 鈥?鏈嶅姟绔師鍨?- [AstaPS](https://github.com/MeChen618/AstaPS) 鈥?7.1.0 鍒嗘敮锛屾湰椤圭洰鐨勬湇鍔＄鍩虹
- [hk4e-patch-universal](https://github.com/KitsukiDev/hk4e-patch-universal) 鈥?瀹㈡埛绔ˉ涓佸熀纭€
- [genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock) 鈥?甯х巼涓婇檺鍙橀噺鐨勫畾浣嶆柟娉?- [ImGui](https://github.com/ocornut/imgui)銆乕MinHook](https://github.com/TsudaKageyu/minhook) 鈥?鑿滃崟鐣岄潰涓庨挬瀛?
## 直接拿来用（不编译）

仓库只放源码。要用现成的二进制，需要下面四个文件，把它们放到对应位置即可：

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
- 天气只改气候（晴/阴/雨/雷/雪），不会触发蒙德城的暴风雨剧情；这是故意的，改成天气区域会让客户端崩。
- 帧率靠外置工具，不在菜单里，原因见 troubleshooting。