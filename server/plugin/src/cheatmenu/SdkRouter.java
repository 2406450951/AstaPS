package cheatmenu;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.server.http.Router;
import io.javalin.Javalin;
import io.javalin.http.Context;
import io.javalin.http.Handler;

/**
 * Guarded SDK endpoints.
 *
 * <p>The client's SDK parses a long, fixed field list out of {@code loadConfig} / {@code getConfig}
 * and gives up with "The client is damaged, please reinstall the client. Error Code:(2,0,4)" when
 * anything is missing. The stock server answers some of these paths with a stub, and Javalin refuses
 * to let a plugin replace an existing route — so the client patch rewrites those paths to
 * {@code /gs71/sdk/*}, which nothing else claims, and the payloads below are byte-for-byte copies of
 * what hk4e-sdk-s.mihoyo.com really returns (only URLs that would leave the machine are changed).
 */
public final class SdkRouter implements Router {

    private static final String VERSION = "7.1.0";
    private static final String PROTOCOL = "OSPRODWin1.0.0";

    @Override
    public void applyRoutes(Javalin javalin) {
        int added = 0;

        // ---- patched paths (authoritative) ---------------------------------------------
        added += add(javalin, "GET", "/gs71/region_list", SdkRouter::regionList);
        added += add(javalin, "POST", "/gs71/region_list", SdkRouter::regionList);
        added += add(javalin, "GET", "/gs71/sdk/loadConfig", SdkRouter::loadConfig);
        added += add(javalin, "POST", "/gs71/sdk/loadConfig", SdkRouter::loadConfig);
        added += add(javalin, "GET", "/gs71/sdk/getConfig", SdkRouter::getConfig);
        added += add(javalin, "POST", "/gs71/sdk/getConfig", SdkRouter::getConfig);
        added += add(javalin, "POST", "/gs71/sdk/compareProtocolVersion", SdkRouter::compareProtocolGlobal);
        added += add(javalin, "GET", "/gs71/sdk/compareProtocolVersion", SdkRouter::compareProtocolGlobal);
        added += add(javalin, "POST", "/gs71/sdk/maPassportConfig", SdkRouter::maPassportConfig);
        added += add(javalin, "GET", "/gs71/sdk/maPassportConfig", SdkRouter::maPassportConfig);

        // ---- paths that are still free on the stock router ------------------------------
        added += add(javalin, "POST", "/hk4e_cn/combo/granter/api/compareProtocolVersion", SdkRouter::compareProtocolGlobal);
        added += add(javalin, "GET", "/hk4e_cn/combo/granter/api/compareProtocolVersion", SdkRouter::compareProtocolGlobal);
        added += add(javalin, "GET", "/hk4e_cn/combo/granter/api/getConfig", SdkRouter::getConfig);
        added += add(javalin, "GET", "/hk4e_cn/mdk/agreement/api/getAgreementInfos", SdkRouter::agreements);
        added += add(javalin, "GET", "/gs71/sdk/getExtList", SdkRouter::extList);
        added += add(javalin, "POST", "/gs71/sdk/getExtList", SdkRouter::extList);
        added += add(javalin, "GET", "/device-fp/api/getExtList", SdkRouter::extList);
        added += add(javalin, "POST", "/device-fp/api/getFp", SdkRouter::deviceFp);
        added += add(javalin, "GET", "/query_security_file", SdkRouter::securityFile);

        Grasscutter.getLogger().info("[CheatMenu] SDK router: {} new route(s) registered", added);
    }

    private static int add(Javalin javalin, String method, String path, Handler handler) {
        try {
            switch (method) {
                case "POST" -> javalin.post(path, handler);
                case "GET" -> javalin.get(path, handler);
                default -> {
                    return 0;
                }
            }
            return 1;
        } catch (Throwable duplicate) {
            Grasscutter.getLogger().debug("[CheatMenu] route {} {} already exists", method, path);
            return 0;
        }
    }

    /**
     * Region list, built here instead of by {@code RegionHandler} so the CN branch can be given
     * {@code sdkenv=1} (mainland test environment) instead of {@code 0} (mainland production).
     * Production mode makes this client demand the anti-addiction/compliance block and, without it,
     * raise "The client is damaged, please reinstall the client. Error Code:(2,0,4)".
     */
    private static void regionList(Context ctx) {
        try {
            var servers = java.util.List.of(
                    emu.grasscutter.net.proto.RegionSimpleInfoOuterClass.RegionSimpleInfo.newBuilder()
                            .setName("os_usa")
                            .setTitle("Grasscutter")
                            .setType("DEV_PUBLIC")
                            .setDispatchUrl("http://127.0.0.1:8088/query_cur_region/os_usa")
                            .build());

            var customConfig = new com.google.gson.JsonObject();
            customConfig.addProperty("sdkenv", "1");
            customConfig.addProperty("checkdevice", "false");
            customConfig.addProperty("loadPatch", "false");
            customConfig.addProperty("showexception", "false");
            customConfig.addProperty("regionConfig", "pm|fk|add");
            customConfig.addProperty("downloadMode", "0");
            var codeSwitch = new com.google.gson.JsonArray();
            codeSwitch.add(4334);
            customConfig.add("codeSwitch", codeSwitch);
            var coverSwitch = new com.google.gson.JsonArray();
            coverSwitch.add(40);
            customConfig.add("coverSwitch", coverSwitch);

            byte[] encoded = customConfig.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);
            emu.grasscutter.utils.Crypto.xor(encoded, emu.grasscutter.utils.Crypto.DISPATCH_KEY);

            var rsp = emu.grasscutter.net.proto.QueryRegionListHttpRspOuterClass.QueryRegionListHttpRsp
                    .newBuilder()
                    .addAllRegionList(servers)
                    .setClientSecretKey(com.google.protobuf.ByteString.copyFrom(
                            emu.grasscutter.utils.Crypto.DISPATCH_SEED))
                    .setClientCustomConfigEncrypted(com.google.protobuf.ByteString.copyFrom(encoded))
                    .setEnableLoginPc(true)
                    .build();

            String body = emu.grasscutter.utils.Utils.base64Encode(rsp.toByteString().toByteArray());
            Grasscutter.getLogger().info("[CheatMenu] /gs71/region_list served ({} bytes, sdkenv=1)", body.length());
            ctx.result(body);
        } catch (Throwable t) {
            Grasscutter.getLogger().error("[CheatMenu] region list build failed", t);
            ctx.status(500).result("region list error");
        }
    }

    /** The 7.1 SDK wants {@code data.protocol} (or the modified-protocol object) to be non-null. */
    private static void compareProtocolGlobal(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("""
                {"retcode":0,"message":"OK","data":{\
                "modified":true,\
                "protocol":{"id":0,"app_id":4,"language":"zh-cn","user_proto":"","priv_proto":"",\
                "major":7,"minimum":0,"create_time":"0","teenager_proto":"","third_proto":""},\
                "protocol_version":"%s","protocolVersion":"%s","send_protocol_version":true,\
                "client_version":"%s","clientVersion":"%s","app_id":"4","channel_id":"1"}}"""
                .formatted(PROTOCOL, PROTOCOL, VERSION, VERSION));
    }

    private static void getConfig(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("""
                {"retcode":0,"message":"OK","data":{\
                "protocol":true,\
                "qr_enabled":true,\
                "log_level":"INFO",\
                "announce_url":"http://127.0.0.1:8088/announcement/index.html",\
                "push_alias_type":1,\
                "disable_ysdk_guard":true,\
                "enable_announce_pic_popup":false,\
                "app_name":"原神",\
                "qr_enabled_apps":{"bbs":false,"cloud":false},\
                "qr_app_icons":{"app":"","bbs":"","cloud":""},\
                "qr_cloud_display_name":"云·原神",\
                "enable_user_center":false,\
                "functional_switch_configs":{},\
                "ugc_protocol":true,\
                "widget_config":null,\
                "protocol_version":"%s",\
                "client_version":"%s",\
                "api_use_domain":false,\
                "enable_protect":false,\
                "enable_protect_global":false,\
                "account_url":"http://127.0.0.1:8088",\
                "account_url_global":"http://127.0.0.1:8088"}}"""
                .formatted(PROTOCOL, VERSION));
    }

    private static void loadConfig(Context ctx) {
        // Verbatim copy of what hk4e-sdk-s.mihoyo.com really answers for client=3&game_key=hk4e_cn
        // (captured 2026-10-27). The SDK parses this field list strictly; inventing or dropping a
        // key is enough to make it declare the client broken. game_key is echoed back so the same
        // handler also serves hk4e_global.
        String gameKey = ctx.queryParam("game_key");
        if (gameKey == null || gameKey.isEmpty()) gameKey = "hk4e_cn";

        ctx.contentType("application/json");
        ctx.result("""
                {"retcode":0,"message":"OK","data":{\
                "id":6,\
                "game_key":"%s",\
                "client":"PC",\
                "identity":"I_IDENTITY",\
                "guest":false,\
                "ignore_versions":"",\
                "scene":"S_ACCOUNT",\
                "name":"原神",\
                "disable_regist":false,\
                "enable_email_captcha":true,\
                "thirdparty":["tp"],\
                "disable_mmt":true,\
                "server_guest":false,\
                "thirdparty_ignore":{},\
                "enable_ps_bind_account":false,\
                "thirdparty_login_configs":{},\
                "initialize_firebase":false,\
                "bbs_auth_login":false,\
                "bbs_auth_login_ignore":[],\
                "fetch_instance_id":false,\
                "enable_flash_login":false,\
                "enable_logo_18":false,\
                "logo_height":"0",\
                "logo_width":"0",\
                "enable_cx_bind_account":false,\
                "firebase_blacklist_devices_switch":false,\
                "firebase_blacklist_devices_version":0,\
                "hoyolab_auth_login":false,\
                "hoyolab_auth_login_ignore":[],\
                "hoyoplay_auth_login":true,\
                "enable_douyin_flash_login":false,\
                "enable_age_gate":false,\
                "enable_age_gate_ignore":[],\
                "enable_br_age_gate":false,\
                "enable_br_age_gate_ignore":[],\
                "enable_eu_age_gate":false,\
                "enable_eu_age_gate_ignore":[],\
                "vn_webview_realname_enabled":false,
                "api_use_domain":false,
                "account_url":"http://127.0.0.1:8088",
                "account_url_global":"http://127.0.0.1:8088",
                "sdk_version":"2.53.0.196"}}"""
                .formatted(gameKey));
        Grasscutter.getLogger().info("[CheatMenu] loadConfig served for game_key={}", gameKey);
    }

    private static void maPassportConfig(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("""
                {"retcode":0,"message":"OK","data":{\
                "support_reactivate_account":false,\
                "enable_ps_bind_account":false,\
                "login_mode":"account_login",\
                "guest_mode":"close",\
                "realperson_mode":"none",\
                "safeguard_type":"none",\
                "apple_login_enabled":false,\
                "facebook_login_enabled":false,\
                "google_login_enabled":false,\
                "twitter_login_enabled":false}}""");
    }

    private static void agreements(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("{\"retcode\":0,\"message\":\"OK\",\"data\":{\"agreements\":[],\"agreement_infos\":[]}}");
    }

    private static void extList(Context ctx) {
        // Verbatim copy of https://public-data-api.mihoyo.com/device-fp/api/getExtList?platform=3
        // (captured 2026-10-27). The risk-control step consumes this list; an empty array is not the
        // same thing as "no extension modules" and makes the SDK treat the client as unregistered.
        ctx.contentType("application/json");
        ctx.result("""
                {"retcode":0,"message":"OK","data":{"code":200,"msg":"ok",\
                "ext_list":["cpuName","deviceModel","deviceName","deviceType","deviceUID",\
                "gpuID","gpuName","gpuAPI","gpuVendor","gpuVersion","gpuMemory","osVersion",\
                "cpuCores","cpuFrequency","gpuVendorID","isGpuMultiTread","memorySize",\
                "screenSize","engineName","addressMAC","packageVersion"],\
                "pkg_list":[],"pkg_str":""}}""");
    }

    private static void deviceFp(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("{\"retcode\":0,\"message\":\"OK\",\"data\":{\"device_fp\":\"38d7f0ed1e28f\",\"code\":200}}");
    }

    private static void securityFile(Context ctx) {
        ctx.contentType("application/json");
        ctx.result("{\"retcode\":0,\"message\":\"OK\",\"data\":{}}");
    }
}
