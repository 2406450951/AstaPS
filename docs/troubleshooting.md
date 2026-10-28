# 鏁呴殰鎺掓煡

## 鐧诲綍鐩稿叧

### 涓€鐩存姤"鐧诲綍鐘舵€佸け鏁?锛屼笉缁欒緭鍏ユ

瀹㈡埛绔紦瀛樹簡鏃?token锛屾嬁瀹冨弽澶嶈皟 `verify` 鑷锛屽け璐ュ氨鍋滃湪閭ｄ釜鐣岄潰銆?
```powershell
powershell -ExecutionPolicy Bypass -File scripts/閲嶇疆鐧诲綍鐘舵€?ps1
```

瀹冨垹涓ゅ涓滆タ锛氭敞鍐岃〃閲岀殑 `__LastUid___h2153286551` / `unity.player_sessionid_h1351336811`锛?浠ュ強 `<瀹㈡埛绔洰褰?\<X>_Data\SDKCaches\HoYoPass\` 涓嬬殑 token 鏂囦欢銆?
### 鐧诲綍椤靛崱鍦ㄥ姞杞斤紝杈撳畬娌″弽搴?
鎸夐『搴忔煡锛?
1. TLS 鍓嶇疆娌¤捣 鈫?`netstat -ano | findstr :443`
2. 鏍硅瘉涔︽病淇′换 鈫?`certutil -store Root | findstr GS71`
3. hosts 娌″啓 鈫?`ping passport-api.hoyoverse.com` 搴旇В鏋愬埌 `127.0.0.1`
4. 鏈嶅姟绔?8088 娌¤捣 鈫?`netstat -ano | findstr :8088`

### 寮?`The client is damaged, please reinstall the client. Error Code:(2,0,4)`

杩欎釜寮圭獥鏄?SDK 鐢?`NtRaiseHardError` 寮圭殑锛堢獥鍙ｅ綊灞?csrss锛屼笉鏄?MessageBox锛夛紝
**瀹冩槸鐧诲綍澶辫触鐨勬敹灏惧姩浣滐紝涓嶆槸宕╂簝鍘熷洜**銆傜櫥褰曟祦绋嬩竴鏃﹂€氫簡瀹冨氨涓嶅啀鍑虹幇銆?
濡傛灉鐧诲綍宸茬粡閫氫簡杩樺弽澶嶅脊锛岃鏄?`verify` 鎴?`exchange` 鐨勫簲绛旂粨鏋勪笉瀵癸紝
鐪?[differences.md](differences.md) 閲岄偅涓や釜 response 绫汇€?
## 鍥芥湇瀹㈡埛绔负浠€涔堝穿

鍥芥湇 7.1 瀹㈡埛绔兘鐧诲綍銆佽兘杩涗笘鐣屻€佷綔寮婁篃鐢熸晥锛屼絾杩涗笘鐣?40~85 绉掑悗蹇呭畾宕╋細

```
0xC0000005  MHYPBase.dll+0x17112B   movzx eax, byte ptr [rdi + rbx]
鏍堬細+0x9804C 鈫?+0x98734 鈫?+0x99294 鈫?+0xAAE9D 鈫?+0x176DEE
```

`mhypbase.dll` 閲岄偅娈垫槸 VM 娲惧彂锛堣璺宠浆琛ㄧ殑瀛楄妭鍐嶇畻鍋忕Щ锛夈€傝繖涓湴鍧€宕╄鏄?`rdi`锛堣〃鍩哄潃锛?鎴?`rbx`锛堢储寮曪級闈炴硶锛屼篃灏辨槸 VM 鐘舵€佸凡缁忓潖浜嗏€斺€斿浗鏈?SDK 鍒ゅ畾鑷繁琚鏀瑰悗璧扮殑閭ｆ潯鍒嗘敮銆?
璇曡繃鐨勬墜娈靛拰缁撴灉锛?
| 鎵嬫 | 缁撴灉 |
|---|---|
| 璺宠繃鍑洪敊鎸囦护锛坮ax 褰掗浂 + RIP+4锛?| 鎹㈠埌 `MHYPBase+0x17110F2` 缁х画宕╋紝鎵撳湴榧?|
| 鍫甸仴娴嬪煙鍚嶏紙`sdk-log-upload` 绛夛級 | 鏃犳晥锛屽穿婧冩椂鏈轰笉鍙?|
| 鏍￠獙鏂囦欢瀹屾暣鎬э紙涓変唤娓呭崟 2743+4858+176 椤癸級 | 鍏ㄩ綈锛屼笉鏄枃浠堕棶棰?|
| 妯℃嫙閿洏杈撳叆缁曡繃缃戦〉鐧诲綍 | VK / Unicode / Ctrl+V / 鎵弿鐮佸叏璇曡繃锛屽埌涓嶄簡鐧诲綍缃戦〉 |

**缁撹锛氭崲鍥介檯鏈嶅鎴风锛岃繖涓棶棰樼洿鎺ユ秷澶便€?* 鍥介檯鏈?`mhypbase.dll` 娌℃湁杩欐潯鍒ゅ畾鍒嗘敮銆?
## 鑿滃崟鐩稿叧

### 鑿滃崟鍛间笉鍑烘潵

- 瀹㈡埛绔洰褰曞繀椤绘湁 `Astrolabe.dll` 鍜?`hk4e-patch.dll` 杩欎袱涓?*鍥哄畾鏂囦欢鍚?*锛屾敼鍚嶄笉鐢熸晥
- 娓告垙鍚姩鏃╂湡灏变細鍔犺浇 `Astrolabe.dll`锛屽鏋滃畠鏄惎鍔ㄥ悗鎵嶆斁杩涘幓鐨勶紝閲嶅惎娓告垙
- 鎹㈣繃鏋勫缓鐨勮瘽纭 DLL 鏄?64 浣嶏紙`dumpbin /headers` 鐪?machine 鏄?x64锛?
### 鑿滃崟鑳藉紑锛屽紑鍏崇偣浜嗘病鍙嶅簲

鑿滃崟鍜屾湇鍔＄鎻掍欢涔嬮棿鐨勯€氶亾鏂簡锛?
```powershell
Invoke-WebRequest http://127.0.0.1:8890/ping -UseBasicParsing | % Content
# 鏈熸湜 ok
```

涓嶉€氬氨鏌ユ彃浠舵湁娌℃湁鍔犺浇锛坄server/plugins/CheatMenu.jar` 瀛樺湪锛熸湇鍔＄鏃ュ織閲屾湁娌℃湁鎻掍欢鍔犺浇璁板綍锛夈€?
### 涓€杩涗笘鐣屽氨宕╋紝宕╂簝鏃ュ織閲屾湁 `Astrolabe.dll`

杩欐槸鎴戜滑鑷繁浠ｇ爜鐨勯攨锛屼笉鏄父鎴忋€傚巻鍙蹭笂鏈変袱娆★細

- `sscanf_s` 鐢?`%c` 娌＄粰闀垮害鍙傛暟锛岃俯鍧忓爢 鈫?鏀规垚 `sscanf` + `%1c`
- 甯х巼鎵弿閬嶅巻鍏ㄦā鍧楀唴瀛橈紝韪╁埌鏈彁浜ら〉 鈫?宸蹭粠瑕嗙洊灞傜Щ闄?
澶嶇幇鏃剁湅 `<瀹㈡埛绔洰褰?\Astrolabe.log`锛岄噷闈細鎵撴ā鍧?鍋忕Щ銆?
## 甯х巼

娓告垙鍐呰缃彧鏈?30 / 60 涓ゆ。銆傝鏇撮珮寰楁敼鍐呭瓨閲岀殑甯х巼鍏ㄥ眬鍙橀噺銆?
**鏈」鐩殑鑿滃崟涓嶅仛杩欎欢浜嬨€?* 鎶?[genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock)
鐨勬敞鍏ラ€昏緫鎼繘鑿滃崟 DLL 鍚庯紝杩涚▼鍐呮壂鎻?il2cpp 娈典細瑙﹀彂瀹㈡埛绔繚鎶わ紝瀹㈡埛绔洿鎺ラ€€鍑猴紙瀹炴祴涓夋锛夛紝
鎵€浠ユ敼鎴愮敤瀹冭嚜宸辩殑鐙珛绋嬪簭锛?
1. 涓嬪畠鐨?self-contained 鐗堬紙鑷甫 .NET锛屼笉鐢ㄨ杩愯鏃讹級
2. 棣栨杩愯浼氬垪鍑烘壘鍒扮殑娓告垙瀹夎 鈥斺€?**娉ㄦ剰瀹冨彲鑳藉垪鍒板浗鏈?*锛岃 Browse 鎵嬪姩閫?   `<瀹㈡埛绔洰褰?\GenshinImpact.exe`
3. FPS 璁?144锛屽嬀 Start Game Automatically
4. **鐢卞畠鍚姩娓告垙**锛屼笉瑕佸厛鎵嬪姩寮€瀹㈡埛绔紝鍚﹀垯瀹冩寕涓嶄笂鍘?
椤哄甫璇存槑锛欰kebi 娌℃湁鏀瑰抚鐜囩殑鍔熻兘锛宍cheat-base/src/cheat-base/cheat/misc/Settings.cpp` 閲屽彧鏈?`Show FPS Indicator` / `Move FPS Indicator`锛堝抚鏁版樉绀猴級銆?44 甯ф潵鑷笂闈㈤偅涓嫭绔嬪伐鍏枫€?
## 鏈嶅姟绔?
### 璧蜂笉鏉?
```powershell
powershell -ExecutionPolicy Bypass -File server/start-server.ps1 -Status
```

鐪嬫槸鍝竴姝ユ寕锛歁ongo 娌¤捣銆?088 琚崰銆乣resources/` 灞傜骇涓嶅銆丣DK 涓嶆槸 21銆?
### 璧锋潵浜嗕絾瀹㈡埛绔繛涓嶄笂

`22101` 鏄?UDP锛岄槻鐏鍙兘鍙斁琛屼簡 TCP銆傛鏌ワ細

```powershell
Get-NetFirewallRule | Where-Object DisplayName -match 'grasscutter|java'
```

### 鎻掍欢鏀逛簡娌＄敓鏁?
`grasscutter.jar` 鎴?`CheatMenu.jar` 琚?java 杩涚▼鍗犵敤鏃舵棤娉曡鐩栥€傚厛 `-Stop` 鍐嶇紪鍐嶈捣銆?
## 天气

菜单的「晴 / 夜阴 / 雨」发的是**气候**（`SceneAreaWeatherNotify.climateType`），
不是天气区域（`weatherAreaId`）。

早先的实现直接调 `player.setWeather(1..3)`，那三个 id 是蒙德的天气区域，各自绑着一个天气 gadget
（70020001 / 70020002 / 70020003，其中区域 3 的 `isDefaultValid=false`）。服务端把 gadget id 发下去，
客户端要去挂载它，挂载失败就闪退。现在固定 `weatherAreaId=0` 且不带 gadget，只换天空和降雨。

想改天气区域（会下雨、刮风、改变环境光），得同时处理 gadget 的 spawn/despawn，光发个包不够。