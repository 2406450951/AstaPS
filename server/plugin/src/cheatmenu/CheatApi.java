package cheatmenu;

import emu.grasscutter.net.packet.BasePacket;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.SceneAreaWeatherNotifyOuterClass.SceneAreaWeatherNotify;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import emu.grasscutter.Grasscutter;
import emu.grasscutter.data.GameData;
import emu.grasscutter.game.entity.EntityAvatar;
import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.EntityItem;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.props.FightProperty;
import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.Scene;

import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * Local control API for the in-game overlay ({@code Gs71Menu.dll}).
 *
 * <p>Everything the overlay toggles resolves here, so the menu can look and feel like Akebi-GC
 * while the authority for damage / drops / HP / time stays on the server where it belongs.
 *
 * <pre>
 *   GET  /snap                plain-text world snapshot (player, entities, live state)
 *   POST /set?k=v&k=v         apply toggles and sliders
 *   POST /cmd?c=name&...      run an action (killall / tp / spawn / give / weather / ...)
 *   GET  /ping                liveness probe
 * </pre>
 *
 * <p>The wire format is deliberately boring (one record per line, space separated) so the C++
 * overlay can parse it with {@code sscanf} instead of pulling in a JSON library.
 */
public final class CheatApi {

    private CheatApi() {}

    public static final int PORT = 8890;

    // ---------------------------------------------------------------- state
    public static volatile boolean god = false;
    public static volatile boolean fall = false;
    public static volatile boolean loot = false;
    public static volatile boolean stamina = false;
    public static volatile boolean autoHeal = false;

    public static volatile boolean timeFreeze = false;
    public static volatile int timeH = 12;
    public static volatile int timeM = 0;
    public static volatile int weather = -1;

    public static volatile float lootRadius = 25f;

    private static volatile boolean running = false;
    private static volatile String lastAction = "ready";
    private static volatile long lastActionAt = 0L;
    /** 客户端 T 键是否按住（按住 T + 点地图 = 传送，传完自动删针） */
    private static volatile boolean tKeyHeld = false;

    // 吸附怪物 / 无限大招 / 技能可用
    public static boolean noclip = false;    // 连续穿墙（会白屏，默认关）
    public static float noclipSpeed = 4f;    // 放大倍数
    public static boolean autoTp = false;   // 扎针即刻传送（大地图点哪传哪）
    public static boolean gravOn = false;   // 反重力（流浪者飞行模拟）
    public static boolean autoint = true;
    public static boolean puzzle = false;   // 自动解神：广域交互所有机关   // 自动交互（宝箱/机关）
    public static int hits = 1;   // 多段伤害：一刀打几次
    public static boolean mobVac = false;
    public static float mobVacRadius = 20f;
    public static boolean burst = false;
    public static boolean skillReady = false;
    public static boolean speedOn = false;
    public static float speedRatio = 1.5f;
    public static boolean flyOn = false;
    public static float flySpeed = 8f;
    public static int flyUp = 0;
    public static int flyDir = 1;
    private static long lastMobVacMs = 0L;
    private static long lastBurstMs = 0L;
    private static long lastSkillMs = 0L;
    private static long lastSpeedMs = 0L;
    private static long lastFlyMs = 0L;

    private static long lastLootMs = 0L;
    private static long lastIntMs = 0L;
    private static long lastGravMs = 0L;
    private static long lastAutoTpMs = 0L;
    private static long lastNoclipMs = 0L;
    private static final java.util.Set<String> seenMarks = new java.util.HashSet<>();
    private static int seenUid = -1;
    private static long lastTimeMs = 0L;
    private static long lastHealMs = 0L;
    private static long lastSnapMs = 0L;
    private static int snapCount = 0;

    private static HttpServer http;
    private static CheatMenu plugin;

    private static final ConcurrentLinkedQueue<String[]> ACTIONS = new ConcurrentLinkedQueue<>();

    // ---------------------------------------------------------------- life cycle

    public static synchronized void start(CheatMenu owner) {
        if (running) return;
        plugin = owner;
        try {
            http = HttpServer.create(new InetSocketAddress("127.0.0.1", PORT), 0);
            http.createContext("/snap", CheatApi::handleSnap);
            http.createContext("/set", CheatApi::handleSet);
            http.createContext("/cmd", CheatApi::handleCmd);
            http.createContext("/ping", ex -> respond(ex, "ok"));
            http.setExecutor(java.util.concurrent.Executors.newFixedThreadPool(2, r -> {
                Thread t = new Thread(r, "CheatApi-http");
                t.setDaemon(true);
                return t;
            }));
            http.start();
            running = true;
            owner.getLogger().info("[CheatMenu] overlay API on http://127.0.0.1:{}/snap", PORT);
        } catch (Throwable t) {
            owner.getLogger().error("[CheatMenu] overlay API failed to start", t);
        }
    }

    public static synchronized void stop() {
        if (http != null) {
            try {
                http.stop(0);
            } catch (Throwable ignored) {
            }
            http = null;
        }
        running = false;
    }

    public static boolean isRunning() {
        return running;
    }

    // ---------------------------------------------------------------- per tick

    /** Applies the overlay's state to every online player and drains queued actions. */
    public static long tickCount;

    public static void tick(CheatMenu owner) {
        if (owner == null) return;
        tickCount++;
        long now = System.currentTimeMillis();

        List<Player> players = owner.onlinePlayers();
        for (Player player : players) {
            try {
                Flags.PlayerFlags f = owner.flags().of(player.getUid());
                f.god = god;
                f.fall = fall;
                f.loot = loot;
                player.setUnlimitedStamina(stamina);
            } catch (Throwable ignored) {
            }
        }

        if (loot && now - lastLootMs >= 300L) {
            lastLootMs = now;
            for (Player player : players) {
                try {
                    owner.vacuum(player, lootRadius);

                } catch (Throwable ignored) {
                }
            }
        }

        // ---- 扎针即刻传送：客户端每扎一根针都会同步到服务端，这里监听新增的标记
        if (autoTp && now - lastAutoTpMs >= 400L) {
            lastAutoTpMs = now;
            for (Player p4 : players) {
                try {
                    var mgr = p4.getMapMarksManager();
                    var marks = mgr == null ? null : mgr.getMapMarks();
                    if (marks == null) continue;
                    if (seenUid != p4.getUid()) {
                        seenUid = p4.getUid();
                        seenMarks.clear();
                        seenMarks.addAll(marks.keySet());
                        continue;   // 第一次只记录，不传
                    }
                    StringBuilder pinDebug = new StringBuilder();
                    for (var e : marks.entrySet()) {
                        if (seenMarks.contains(e.getKey())) continue;
                        seenMarks.add(e.getKey());
                        var mark = e.getValue();
                        if (mark != null) {
                            pinDebug.append(String.format(Locale.ROOT, "new[t=%s scene=%d tkey=%d]; ",
                                    mark.getMapMarkPointType(), mark.getSceneId(), tKeyHeld ? 1 : 0));
                        }
                        if (mark == null || mark.getPosition() == null) continue;
                        if (mark.getSceneId() != p4.getSceneId()) continue;
                        // 只认玩家自己扎的针（SPECIAL）；NPC/任务/矿点/怪物标点一律忽略
                        if (mark.getMapMarkPointType()
                                != emu.grasscutter.net.proto.MapMarkPointTypeOuterClass
                                        .MapMarkPointType.MapMarkPointType_SPECIAL) {
                            continue;
                        }
                        // 只有按住 T 才传送；否则当作普通扎针，不打扰
                        if (!tKeyHeld) continue;
                        Position target = mark.getPosition();
                        float y = target.getY();
                        if (y <= 1.0f) {
                            Position cur = p4.getPosition();
                            y = (cur == null ? 200f : cur.getY()) + 1f;
                        }
                        Position safe = new Position(target.getX(), y + 1f, target.getZ());
                        {
                            p4.teleport(safe);
                            lastAction = String.format(Locale.ROOT, "autotp: %.0f,%.0f,%.0f",
                                    safe.getX(), safe.getY(), safe.getZ());
                            // 按住 T 触发时，把钉子删掉，地图保持干净
                            if (tKeyHeld && mgr != null) {
                                try {
                                    mgr.removeMapMark(target);
                                    seenMarks.remove(e.getKey());
                                } catch (Throwable ignored) {
                                }
                            }
                        }
                    }
                    if (pinDebug.length() > 0) lastAction = pinDebug.toString();
                    // 清理已被删除的针
                    seenMarks.retainAll(marks.keySet());
                } catch (Throwable ignored) {
                }
            }
        }

        if (gravOn && now - lastGravMs >= 1000L) {
            lastGravMs = now;
            for (Player p3 : players) {
                try {
                    owner.applyGravity(p3, -2.0f);
                } catch (Throwable ignored) {
                }
            }
        }

        if (autoint && now - lastIntMs >= 500L) {
            lastIntMs = now;
            for (Player p2 : players) {
                if (p2.getScene() == null) continue;
                try {
                    int opened = owner.interactAll(p2, lootRadius);
                    if (opened > 0) lastAction = "chest opened x" + opened;
                    if (puzzle) {
                        int n2 = owner.interactBroad(p2, lootRadius, true);
                        if (n2 > 0) lastAction = "puzzle interact x" + n2;
                    }
                } catch (Throwable ignored) {
                }
            }
        }

        if (noclip && now - lastNoclipMs >= 80L) {
            lastNoclipMs = now;
            for (Player p5 : players) {
                if (p5.getScene() == null) continue;
                try {
                    float moved = owner.noclipStep(p5, noclipSpeed);
                    if (moved > 0f) {
                        lastAction = String.format(Locale.ROOT, "noclip: +%.1fm", moved);
                    }
                } catch (Throwable ignored) {
                }
            }
        }

        if (timeFreeze && now - lastTimeMs >= 2000L) {
            lastTimeMs = now;
            for (Player player : players) {
                try {
                    if (player.getWorld() != null) player.getWorld().changeTime(timeH, timeM);
                } catch (Throwable ignored) {
                }
            }
        }

        if (autoHeal && now - lastHealMs >= 500L) {
            lastHealMs = now;
            for (Player player : players) {
                try {
                    owner.topUpHp(player);
                } catch (Throwable ignored) {
                }
            }
        }

        if (mobVac && !players.isEmpty() && now - lastMobVacMs >= 400L) {
            lastMobVacMs = now;
            for (Player player : players) {
                try {
                    owner.mobVacuum(player, mobVacRadius);
                } catch (Throwable ignored) {
                }
            }
        }

        if (burst && !players.isEmpty() && now - lastBurstMs >= 1000L) {
            lastBurstMs = now;
            for (Player player : players) {
                try {
                    owner.refillBurst(player);
                } catch (Throwable ignored) {
                }
            }
        }

        if (flyOn && !players.isEmpty() && now - lastFlyMs >= 800L) {
            lastFlyMs = now;
            // 800ms 一步，步长 = 速度 × 0.8，手感是"跳跃式飞行"
            float meters = Math.max(2f, Math.min(60f, flySpeed * 0.8f));
            for (Player player : players) {
                try {
                    if (flyUp != 0) owner.flyLift(player, flyUp > 0 ? 6f : -6f);
                    else owner.flyStep(player, meters * flyDir);
                } catch (Throwable ignored) {
                }
            }
        }

        if (speedOn && !players.isEmpty() && now - lastSpeedMs >= 1000L) {
            lastSpeedMs = now;
            for (Player player : players) {
                try {
                    owner.applyMoveSpeed(player, speedRatio - 1.0f);
                } catch (Throwable ignored) {
                }
            }
        }

        if (skillReady && !players.isEmpty() && now - lastSkillMs >= 500L) {
            lastSkillMs = now;
            for (Player player : players) {
                try {
                    owner.pingSkillReady(player);   // 让客户端知道"可以放"
                    owner.resetSkillCd(player);     // 把冷却清零
                } catch (Throwable ignored) {
                }
            }
        }

        String[] action;
        int budget = 16;
        while (budget-- > 0 && (action = ACTIONS.poll()) != null) {
            try {
                runAction(owner, action[0], action[1]);
            } catch (Throwable t) {
                lastAction = "error: " + t;
                lastActionAt = now;
            }
        }
    }

    // ---------------------------------------------------------------- actions

    /** 地图针的 Y 经常是 0，直接传会摔死或者卡进地里。用玩家当前高度兜底。 */
    private static Position safePos(Player player, Position p) {
        if (p == null) return null;
        float y = p.getY();
        if (y <= 1.0f) {
            Position cur = player.getPosition();
            y = cur == null ? 200f : cur.getY();
        }
        return new Position(p.getX(), y + 1.0f, p.getZ());
    }

    private static void runAction(CheatMenu owner, String cmd, String arg) {
        List<Player> players = owner.onlinePlayers();
        Player actor = players.isEmpty() ? null : players.get(0);
        if (actor == null) {
            lastAction = "no player online";
            lastActionAt = System.currentTimeMillis();
            return;
        }
        Map<String, String> q = parseQuery(arg);

        switch (cmd.toLowerCase(Locale.ROOT)) {
            case "killall" -> lastAction = "killall: " + owner.killAll(actor) + " killed";
            case "heal" -> {
                owner.topUpHp(actor);
                lastAction = "heal";
            }
            case "lootnow" -> lastAction = "lootnow: " + owner.vacuum(actor, lootRadius);
            case "aura_tick" -> lastAction = "aura: " + owner.auraTick(actor) + " hits";
            case "chest" -> {
                EntityGadget chest = owner.nearestChest(actor);
                if (chest == null) {
                    lastAction = "chest: none nearby";
                } else {
                    Position p = chest.getPosition();
                    actor.teleport(new Position(p.getX(), p.getY() + 1.0f, p.getZ()));
                    lastAction = "chest: " + chest.getGadgetId();
                }
            }
            case "tp" -> {
                float x = f(q, "x", 0);
                float y = f(q, "y", 0);
                float z = f(q, "z", 0);
                actor.teleport(new Position(x, y, z));
                lastAction = String.format(Locale.ROOT, "tp: %.1f %.1f %.1f", x, y, z);
            }
            case "time" -> {
                int h = (int) f(q, "h", timeH);
                int m = (int) f(q, "m", timeM);
                if (actor.getWorld() != null) {
                    // A world saved with locked time ignores every clock change, which makes the
                    // menu look broken. Clear the lock the first time we touch the clock.
                    if (actor.getWorld().isTimeLocked()) actor.getWorld().lockTime(false);
                    actor.getWorld().changeTime(h, m);
                }
                timeH = h;
                timeM = m;
                lastAction = String.format(Locale.ROOT, "time: %02d:%02d", h, m);
            }
            case "weather" -> {
                // Two independent knobs:
                //   id   = climate (1 sunny / 2 cloudy / 3 rain / 4 storm / 5 snow)
                //   area = weather area id (1..4 are Mondstadt); 0 means "do not touch the area"
                // The area changes sky and lighting. Its weather GADGET is deliberately never sent:
                // mounting that gadget is what used to crash the client.
                int climate = (int) f(q, "id", 1);
                int area = (int) f(q, "area", 0);
                if (actor.getWorld() != null && actor.getWorld().isTimeLocked())
                    actor.getWorld().lockTime(false);
                var notify = SceneAreaWeatherNotify.newBuilder()
                        .setWeatherAreaId(area)
                        .setClimateType(climate)
                        .build();
                BasePacket packet = new BasePacket(PacketOpcodes.SceneAreaWeatherNotify);
                packet.setData(notify);
                actor.sendPacket(packet);
                weather = climate;
                lastAction = "weather: climate=" + climate + " area=" + area;
            }            case "anchors" -> {
                var list = owner.anchorList(actor);
                StringBuilder sb = new StringBuilder("anchors(" + list.size() + "):");
                int idx = 0;
                for (int[] a : list) {
                    Position pos = owner.anchorPos(a[1], a[0]);
                    sb.append(" [").append(idx++).append("]id=").append(a[0]);
                    if (pos != null)
                        sb.append("@").append(Math.round(pos.getX())).append(",")
                          .append(Math.round(pos.getY())).append(",")
                          .append(Math.round(pos.getZ()));
                    if (idx >= 12) break;
                }
                lastAction = sb.toString();
            }
            case "goanchor" -> {
                int want = (int) f(q, "i", 0);
                var list = owner.anchorList(actor);
                lastAction = "goanchor: no such index";
                if (want >= 0 && want < list.size()) {
                    int[] a = list.get(want);
                    Position pos = owner.anchorPos(a[1], a[0]);
                    if (pos != null) {
                        pos = safePos(actor, pos);
                        actor.teleport(pos);
                        lastAction = "goanchor: " + want + " (" + Math.round(pos.getX()) + ","
                                + Math.round(pos.getY()) + "," + Math.round(pos.getZ()) + ")";
                    }
                }
            }
            case "fly" -> {
                flyOn = !"0".equals(q.getOrDefault("on", "1"));
                flySpeed = Math.max(1f, Math.min(60f, f(q, "speed", flySpeed)));
                if (q.containsKey("up")) flyUp = (int) f(q, "up", flyUp);
                if (q.containsKey("dir")) flyDir = ((int) f(q, "dir", flyDir)) >= 0 ? 1 : -1;
                lastAction = "fly: " + (flyOn ? "on" : "off") + " speed=" + Math.round(flySpeed)
                        + " up=" + flyUp + " dir=" + flyDir;
            }
            case "speed" -> {
                speedRatio = Math.max(1.0f, Math.min(5.0f, f(q, "r", speedRatio)));
                speedOn = !"0".equals(q.getOrDefault("on", "1"));
                boolean ok = speedOn ? owner.applyMoveSpeed(actor, speedRatio - 1.0f)
                                     : owner.clearMoveSpeed(actor);
                lastAction = "speed: " + (speedOn ? "+" + Math.round((speedRatio - 1f) * 100) + "%" : "off")
                        + (ok ? "" : " (no avatar?)");
            }
            case "questnow" -> {
                // 列出玩家当前的主线任务与子任务状态（用于排查卡任务）
                try {
                    var qm = actor.getQuestManager();
                    var actives = qm == null ? null : qm.getActiveMainQuests();
                    if (actives == null || actives.isEmpty()) {
                        lastAction = "questnow: none";
                    } else {
                        StringBuilder sb = new StringBuilder("questnow:");
                        int i = 0;
                        for (var mq : actives) {
                            if (i++ >= 6) break;
                            sb.append(" main=");
                            try {
                                var subs = mq.getActiveQuests();
                                if (subs != null) {
                                    int j = 0;
                                    for (var sq : subs) {
                                        if (j++ >= 4) break;
                                        sb.append("|").append(sq.getSubQuestId()).append(":")
                                          .append(sq.getState());
                                    }
                                }
                            } catch (Throwable ignored) {
                            }
                        }
                        lastAction = sb.toString();
                    }
                } catch (Throwable t) {
                    lastAction = "questnow: error " + t;
                }
            }
            case "marks" -> {
                // 调试：把服务端收到的全部地图标点打出来，看客户端到底同步了什么
                var mgr = actor.getMapMarksManager();
                var marks = mgr == null ? null : mgr.getMapMarks();
                if (marks == null || marks.isEmpty()) {
                    lastAction = "marks: none";
                } else {
                    StringBuilder sb = new StringBuilder("marks(" + marks.size() + "):");
                    int i = 0;
                    for (var m : marks.values()) {
                        if (i++ >= 10) break;
                        var p2 = m.getPosition();
                        sb.append(" [").append(i - 1).append("]q=").append(m.getQuestId())
                          .append(",t=").append(m.getMapMarkPointType())
                          .append(",sc=").append(m.getSceneId());
                        if (p2 != null)
                            sb.append("@").append(Math.round(p2.getX())).append(",")
                              .append(Math.round(p2.getY())).append(",")
                              .append(Math.round(p2.getZ()));
                    }
                    lastAction = sb.toString();
                }
            }
            case "ncstep" -> {
                // 单次穿墙步进：沿面朝方向瞬移 d 米。单次 teleport 不会触发加载页。
                float d = (float) f(q, "d", 5.0f);
                if (d < 0.5f) d = 0.5f;
                if (d > 60.f) d = 60.f;
                Position cur = actor.getPosition();
                if (cur == null) {
                    lastAction = "ncstep: no position";
                } else {
                    double yaw = 0;
                    try {
                        yaw = actor.getRotation() == null ? 0 : actor.getRotation().getY();
                    } catch (Throwable ignored) {
                    }
                    // 原神朝向：yaw=0 面向 +Z；角度按 Z 轴顺时针
                    double rad = Math.toRadians(yaw);
                    float nx = (float) (cur.getX() + Math.sin(rad) * d);
                    float nz = (float) (cur.getZ() + Math.cos(rad) * d);
                    float ny = cur.getY() + 0.5f;
                    // 垂直方向：up=1 上浮 / up=-1 下降
                    float up = (float) f(q, "up", 0f);
                    if (up != 0f) ny += up * d;
                    String mode = q == null ? "" : q.getOrDefault("mode", "");
                    boolean smooth = mode.contains("mode=sm");
                    if (smooth) {
                        boolean ok = owner.smoothMove(actor, nx, ny, nz);
                        lastAction = String.format(Locale.ROOT, "ncstep(sm=%s): %.1fm -> %.0f,%.0f,%.0f",
                                ok, d, nx, ny, nz);
                    } else {
                        actor.teleport(new Position(nx, ny, nz));
                        lastAction = String.format(Locale.ROOT, "ncstep: %.1fm -> %.0f,%.0f,%.0f", d,
                                nx, ny, nz);
                    }
                }
            }
            case "allanchors" -> {
                // 列出玩家在所有场景里解锁过的传送点（跨场景传送用）
                StringBuilder sb = new StringBuilder("allanchors:");
                int idx = 0;
                try {
                    var map = actor.getUnlockedScenePoints();
                    if (map == null || map.isEmpty()) {
                        lastAction = "allanchors: none";
                    } else {
                        for (var e : map.entrySet()) {
                            int sceneId = e.getKey();
                            var ids = e.getValue();
                            if (ids == null || ids.isEmpty()) continue;
                            sb.append(" [s").append(sceneId).append(":").append(ids.size()).append("]");
                            int shown = 0;
                            for (int pid : ids) {
                                if (shown++ >= 3) break;
                                Position pos = owner.anchorPos(sceneId, pid);
                                sb.append(" ").append(sceneId).append("/").append(pid);
                                if (pos != null)
                                    sb.append("@").append(Math.round(pos.getX())).append(",")
                                      .append(Math.round(pos.getY())).append(",")
                                      .append(Math.round(pos.getZ()));
                                idx++;
                                if (idx >= 12) break;
                            }
                            if (idx >= 12) break;
                        }
                        lastAction = sb.toString();
                    }
                } catch (Throwable t) {
                    lastAction = "allanchors: error " + t;
                }
            }
            case "gotopoint" -> {
                int sceneId = (int) f(q, "s", 0);
                int pointId = (int) f(q, "id", 0);
                Position pos = owner.anchorPos(sceneId, pointId);
                if (pos == null) {
                    lastAction = "gotopoint: point not found";
                } else {
                    // 跨场景：先切场景（会走一次加载，但只有一次），再定位
                    if (sceneId != actor.getSceneId()) {
                        try {
                            var entry = emu.grasscutter.data.GameData
                                    .getScenePointEntryById(sceneId, pointId);
                            if (entry != null && entry.getPointData() != null) {
                                actor.getWorld().transferPlayerToScene(actor, sceneId,
                                        entry.getPointData().getTranPos() != null
                                                ? entry.getPointData().getTranPos()
                                                : entry.getPointData().getPos());
                                lastAction = String.format(Locale.ROOT,
                                        "gotopoint: scene %d @ %.0f,%.0f,%.0f", sceneId,
                                        pos.getX(), pos.getY(), pos.getZ());
                            } else {
                                lastAction = "gotopoint: no scene entry";
                            }
                        } catch (Throwable t) {
                            lastAction = "gotopoint: transfer failed " + t;
                        }
                    } else {
                        Position safe = new Position(pos.getX(), pos.getY() + 1f, pos.getZ());
                        actor.teleport(safe);
                        lastAction = String.format(Locale.ROOT, "gotopoint: %.0f,%.0f,%.0f",
                                safe.getX(), safe.getY(), safe.getZ());
                    }
                }
            }
            case "itemgd" -> {
                int lo = (int) fl(q.getOrDefault("lo", "100001"), 100001);
                int hi = (int) fl(q.getOrDefault("hi", "100010"), 100010);
                var sb4 = new StringBuilder("itemgd [" + lo + ".." + hi + "]\n");
                var imap = GameData.getItemDataMap();
                for (int i = lo; i <= hi; i++) {
                    var it2 = imap == null ? null : imap.get(i);
                    if (it2 == null) {
                        sb4.append("  ").append(i).append("  <missing>\n");
                        continue;
                    }
                    int g4 = 0;
                    try {
                        g4 = it2.getGadgetId();
                    } catch (Throwable ignored) {
                    }
                    sb4.append("  ").append(i).append("  gadgetId=").append(g4).append('\n');
                }
                owner.getLogger().info(sb4.toString());
                lastAction = "itemgd done";
            }
            case "itemscan" -> {
                String match2 = q.getOrDefault("match", "Oculus");
                String mu2 = match2.toUpperCase(java.util.Locale.ROOT);
                var map2 = GameData.getItemDataMap();
                var hb = new StringBuilder();
                int c2 = 0, t2 = 0;
                if (map2 != null) {
                    t2 = map2.size();
                    for (var en2 : map2.entrySet()) {
                        var it = en2.getValue();
                        if (it == null) continue;
                        String nm;
                        String js;
                        try {
                            nm = String.valueOf(it.getEffectName());
                        } catch (Throwable t) {
                            nm = "";
                        }
                        try {
                            js = String.valueOf(it.getIcon());
                        } catch (Throwable t) {
                            js = "";
                        }
                        int gidIt = 0;
                        try {
                            gidIt = it.getGadgetId();
                        } catch (Throwable ignored) {
                        }
                        String hay = nm + " " + js + " " + gidIt;
                        if (!hay.toUpperCase(java.util.Locale.ROOT).contains(mu2)) continue;
                        hb.append("  ").append(en2.getKey()).append("  gadgetId=").append(gidIt)
                                .append("  icon=").append(js).append("  eff=").append(nm).append('\n');
                        if (++c2 >= 40) break;
                    }
                }
                owner.getLogger().info("itemscan [" + match2 + "] total=" + t2 + " hits=" + c2 + "\n" + hb);
                lastAction = "itemscan " + match2 + ": " + c2 + " hit(s) of " + t2;
            }
            case "gadscan" -> {
                String match = q.getOrDefault("match", "Oculus");
                String mu = match.toUpperCase(java.util.Locale.ROOT);
                var map = GameData.getGadgetDataMap();
                var hits = new StringBuilder();
                int count = 0, total = 0;
                if (map != null) {
                    total = map.size();
                    for (var en : map.entrySet()) {
                        var gd = en.getValue();
                        String jn = gd == null ? null : gd.getJsonName();
                        if (jn == null) continue;
                        if (!jn.toUpperCase(java.util.Locale.ROOT).contains(mu)) continue;
                        hits.append("  ").append(en.getKey()).append("  ").append(jn).append('\n');
                        if (++count >= 50) break;
                    }
                }
                owner.getLogger().info("gadscan [" + match + "] total=" + total + " hits=" + count + "\n" + hits);
                lastAction = "gadscan " + match + ": " + count + " hit(s) of " + total + " gadget data";
            }
            case "gadids" -> {
                Scene sc3 = actor.getScene();
                java.util.TreeMap<Integer, String> ids = new java.util.TreeMap<>();
                if (sc3 != null) {
                    for (var ge3 : sc3.getEntities().values()) {
                        if (!(ge3 instanceof EntityGadget g3)) continue;
                        int gid3 = g3.getGadgetId();
                        if (ids.containsKey(gid3)) continue;
                        String jn3 = "?";
                        try {
                            var gd3 = GameData.getGadgetDataMap().get(gid3);
                            if (gd3 != null && gd3.getJsonName() != null) jn3 = gd3.getJsonName();
                        } catch (Throwable ignored) {
                        }
                        ids.put(gid3, jn3);
                    }
                }
                var sb3 = new StringBuilder("gadids count=" + ids.size() + "\n");
                int shown3 = 0;
                for (var en3 : ids.entrySet()) {
                    sb3.append("  ").append(en3.getKey()).append("  ").append(en3.getValue()).append('\n');
                    if (++shown3 >= 80) break;
                }
                owner.getLogger().info(sb3.toString());
                lastAction = "gadids: " + ids.size() + " distinct gadget id(s), see server.log";
            }
            case "gadtags" -> {
                Scene sc = actor.getScene();
                java.util.Map<String, Integer> cnt = new java.util.TreeMap<>();
                int scanned = 0;
                if (sc != null) {
                    for (var ge : sc.getEntities().values()) {
                        if (!(ge instanceof EntityGadget g2)) continue;
                        scanned++;
                        try {
                            var gd2 = GameData.getGadgetDataMap().get(g2.getGadgetId());
                            if (gd2 == null || gd2.getTags() == null) continue;
                            for (String tg : gd2.getTags()) {
                                if (tg == null) continue;
                                cnt.merge(tg, 1, Integer::sum);
                            }
                        } catch (Throwable ignored) {
                        }
                    }
                }
                var sb2 = new StringBuilder("gadtags scanned=" + scanned + "\n");
                int shown = 0;
                for (var en : cnt.entrySet()) {
                    sb2.append("  ").append(en.getKey()).append(" x").append(en.getValue()).append('\n');
                    if (++shown >= 60) break;
                }
                owner.getLogger().info(sb2.toString());
                lastAction = "gadtags: scanned=" + scanned + " tags=" + cnt.size() + " (see server.log)";
            }
            case "gadgettp" -> {
                int idmin = (int) fl(q.getOrDefault("idmin", "0"), 0);
                int idmax = (int) fl(q.getOrDefault("idmax", "0"), 0);
                String tag = q.getOrDefault("tag", "");
                Position target =
                        (idmax > 0)
                                ? owner.nearestGadgetByIdRange(actor, idmin, idmax)
                                : owner.nearestGadgetByTag(actor, tag);
                if (target != null) {
                    try {
                        actor.teleport(new Position(target.getX(), target.getY() + 1.5f, target.getZ()));
                    } catch (Throwable ignored) {
                    }
                }
                lastAction =
                        target == null
                                ? ("gadgettp: no target in this scene")
                                : String.format(
                                        java.util.Locale.ROOT,
                                        "gadgettp %s: %.0f,%.0f,%.0f",
                                        tag,
                                        target.getX(),
                                        target.getY(),
                                        target.getZ());
            }
            case "chesttp" -> {
                Position target = owner.chestTeleport(actor);
                lastAction = target == null
                        ? "chesttp: no chest in this scene"
                        : String.format(Locale.ROOT, "chesttp: %.0f,%.0f,%.0f", target.getX(),
                                target.getY(), target.getZ());
            }
            case "interactnow" -> {
                int n = owner.interactAll(actor, lootRadius);
                lastAction = "interactnow: opened " + n;
            }
            case "questtp" -> {
                Position[] out = new Position[1];
                String label = owner.questTarget(actor, out);
                if (label == null || out[0] == null) {
                    lastAction = "questtp: no quest marker in this scene";
                } else {
                    out[0] = safePos(actor, out[0]);
                    actor.teleport(out[0]);
                    lastAction = String.format(Locale.ROOT, "questtp: %s @ %.0f,%.0f,%.0f", label,
                            out[0].getX(), out[0].getY(), out[0].getZ());
                }
            }
            case "mapstat" -> {
            // 诊断：报告运行时真实的解锁状态
            try {
                int scene = actor.getSceneId();
                var pts = actor.getUnlockedScenePoints(scene);
                var areas = actor.getUnlockedSceneAreas(scene);
                var marks = actor.getMapMarksManager() == null ? null
                        : actor.getMapMarksManager().getMapMarks();
                lastAction = String.format(Locale.ROOT,
                        "mapstat: scene=%d points=%d areas=%d openStates=%d marks=%d",
                        scene, pts == null ? -1 : pts.size(),
                        areas == null ? -1 : areas.size(),
                        actor.getOpenStates() == null ? -1 : actor.getOpenStates().size(),
                        marks == null ? -1 : marks.size());
            } catch (Throwable t) {
                lastAction = "mapstat err: " + t;
            }
        }
        case "mapgo" -> {
                Position pin = owner.mapPinPosition(actor);
                if (pin == null) {
                    lastAction = "mapgo: no pin in this scene";
                } else {
                    pin = safePos(actor, pin);
                    actor.teleport(pin);
                    lastAction = String.format(Locale.ROOT, "mapgo: %.1f %.1f %.1f",
                            pin.getX(), pin.getY(), pin.getZ());
                }
            }
            case "mobvacnow" -> lastAction = "mobvac: " + owner.mobVacuum(actor, mobVacRadius);
            case "unlock" -> {
                if (actor.getWorld() != null) {
                    actor.getWorld().lockTime(false);
                    lastAction = "time unlocked";
                } else {
                    lastAction = "no world";
                }
            }
            case "give" -> {
                int id = (int) f(q, "id", 202);
                int n = (int) f(q, "n", 1);
                lastAction = "give: " + invoke(actor, "give " + id + " x" + n, q);
            }
            case "spawn" -> {
                int id = (int) f(q, "id", 21010101);
                int lvl = (int) f(q, "lvl", 90);
                int n = (int) f(q, "n", 1);
                lastAction = "spawn: " + invoke(actor, "spawn " + id + " " + lvl + " " + n, q);
            }
            case "scene" -> {
                int id = (int) f(q, "id", 3);
                lastAction = "scene: " + invoke(actor, "tpscene " + id, q);
            }
            case "command", "raw" -> {
                String raw = q.getOrDefault("text", "");
                lastAction = "cmd: " + invoke(actor, raw, q);
            }
            case "bookmark" -> {
                String name = q.getOrDefault("name", "home");
                Position p = actor.getPosition();
                if (p != null) Remote.saveBookmark(name, p);
                lastAction = "bookmark saved: " + name;
            }
            case "go" -> {
                String name = q.getOrDefault("name", "home");
                Position p = Remote.loadBookmark(name);
                if (p == null) {
                    lastAction = "bookmark missing: " + name;
                } else {
                    actor.teleport(p);
                    lastAction = "teleported: " + name;
                }
            }
            default -> lastAction = "unknown action: " + cmd;
        }
        lastActionAt = System.currentTimeMillis();
    }

    private static String invoke(Player actor, String line, Map<String, String> q) {
        try {
            String text = line.startsWith("/") ? line.substring(1) : line;
            Grasscutter.getCommandMap().invoke(actor, actor, text);
            return "ok";
        } catch (Throwable t) {
            return "failed: " + t;
        }
    }

    // ---------------------------------------------------------------- http

    private static void handleSnap(HttpExchange ex) {
        try {
            String body = snapshot();
            byte[] out = body.getBytes(StandardCharsets.UTF_8);
            ex.getResponseHeaders().add("Content-Type", "text/plain; charset=utf-8");
            ex.sendResponseHeaders(200, out.length);
            try (OutputStream os = ex.getResponseBody()) {
                os.write(out);
            }
            lastSnapMs = System.currentTimeMillis();
            snapCount++;
        } catch (Throwable t) {
            try {
                respond(ex, "error " + t);
            } catch (Throwable ignored) {
            }
        }
    }

    private static void handleSet(HttpExchange ex) {
        try {
            Map<String, String> q = parseQuery(rawQuery(ex));
            applySettings(q);
            respond(ex, "ok");
        } catch (Throwable t) {
            respond(ex, "error " + t);
        }
    }

    private static void handleCmd(HttpExchange ex) {
        try {
            String raw = rawQuery(ex);
            Map<String, String> q = parseQuery(raw);
            String cmd = q.getOrDefault("c", "");
            if (!cmd.isEmpty()) ACTIONS.add(new String[] { cmd, raw });
            respond(ex, "ok " + cmd);
        } catch (Throwable t) {
            respond(ex, "error " + t);
        }
    }

    private static void applySettings(Map<String, String> q) {
        CheatMenu owner = plugin;
        for (Map.Entry<String, String> e : q.entrySet()) {
            String k = e.getKey().toLowerCase(Locale.ROOT);
            String v = e.getValue();
            switch (k) {
                case "god" -> god = b(v);
                case "fall" -> fall = b(v);
                case "loot" -> loot = b(v);
                case "stamina" -> stamina = b(v);
                case "autoheal" -> autoHeal = b(v);
                case "timefreeze" -> timeFreeze = b(v);
                case "timeh" -> timeH = clamp((int) fl(v, timeH), 0, 23);
                case "timem" -> timeM = clamp((int) fl(v, timeM), 0, 59);
                case "weather" -> weather = (int) fl(v, weather);
                case "lootr" -> lootRadius = Math.max(1f, Math.min(120f, fl(v, lootRadius)));
                case "noclip" -> noclip = b(v);
                case "noclipspeed" -> noclipSpeed = Math.max(1f, Math.min(20f, fl(v, noclipSpeed)));
                case "autotp" -> autoTp = b(v);
        case "tkey" -> tKeyHeld = b(v);
                case "grav" -> gravOn = b(v);
                case "autoint" -> autoint = b(v);
            case "puzzle" -> puzzle = b(v);
                case "hits" -> hits = clamp((int) fl(v, hits), 1, 30);
                case "mobvac" -> mobVac = b(v);
                case "mobvarr" -> mobVacRadius = Math.max(5f, Math.min(80f, fl(v, mobVacRadius)));
                case "burst" -> burst = b(v);
                case "skillready" -> skillReady = b(v);
                case "mult" -> {
                    if (owner != null) owner.mult = Math.max(0.1f, Math.min(10000f, fl(v, owner.mult)));
                }
                case "aura" -> {
                    if (owner != null) owner.aura = b(v);
                }
                case "aurar" -> {
                    if (owner != null) owner.auraRadius = Math.max(1f, Math.min(120f, fl(v, owner.auraRadius)));
                }
                case "auradmg" -> {
                    if (owner != null) owner.auraDamage = Math.max(1f, Math.min(1.0e9f, fl(v, owner.auraDamage)));
                }
                case "esp" -> {
                    if (owner != null) owner.esp = b(v);
                }
                default -> {
                }
            }
        }
    }

    /** Snapshot: one record per line, first token = record type. */
    public static String snapshot() {
        CheatMenu owner = plugin;
        StringBuilder sb = new StringBuilder(4096);
        long now = System.currentTimeMillis();

        sb.append("v 1\n");
        sb.append("up ").append(now - lastStartMs()).append('\n');
        sb.append("tick ").append(tickCount).append('\n');
        if (plugin != null) {
            sb.append("loot ").append(plugin.lastLootScanned).append(' ')
              .append(plugin.lastLootPicked).append('\n');
            sb.append("mob ").append(plugin.lastMobPulled).append('\n');
        }

        List<Player> players = owner == null ? List.of() : owner.onlinePlayers();
        Player me = players.isEmpty() ? null : players.get(0);

        if (me == null) {
            sb.append("online 0\n");
        } else {
            Position p = me.getPosition();
            Position r = me.getRotation();
            sb.append("online ").append(players.size()).append('\n');
            sb.append("uid ").append(me.getUid()).append('\n');
            sb.append("nick ").append(clean(me.getNickname())).append('\n');
            sb.append("scene ").append(me.getScene() == null ? 0 : me.getScene().getId()).append('\n');
            if (p != null) {
                sb.append("pos ").append(p.getX()).append(' ').append(p.getY()).append(' ').append(p.getZ()).append('\n');
            }
            if (r != null) {
                sb.append("rot ").append(r.getX()).append(' ').append(r.getY()).append(' ').append(r.getZ()).append('\n');
            }
            try {
                EntityAvatar avatar = me.getTeamManager().getCurrentAvatarEntity();
                if (avatar != null) {
                    sb.append("hp ")
                            .append(avatar.getFightProperty(FightProperty.FIGHT_PROP_CUR_HP)).append(' ')
                            .append(avatar.getFightProperty(FightProperty.FIGHT_PROP_MAX_HP)).append('\n');
                }
            } catch (Throwable ignored) {
            }
            try {
                if (me.getWorld() != null) sb.append("world ").append(me.getWorld().getGameTimeHours()).append('\n');
            } catch (Throwable ignored) {
            }

            Scene scene = me.getScene();
            if (scene != null && scene.getEntities() != null && p != null) {
                List<GameEntity> list = new ArrayList<>(scene.getEntities().values());
                StringBuilder body = new StringBuilder(2048);
                int n = 0;
                for (GameEntity e : list) {
                    Position ep = e.getPosition();
                    if (ep == null) continue;
                    double dx = ep.getX() - p.getX();
                    double dy = ep.getY() - p.getY();
                    double dz = ep.getZ() - p.getZ();
                    double dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > 220.0) continue;

                    char kind;
                    int typeId = e.getEntityTypeId();
                    if (e instanceof EntityMonster) kind = 'M';
                    else if (e instanceof EntityGadget gd0) {
                        // 按 gadgetId 段细分（实测：GadgetData.getTags() 全是 "item"，不可用）
                        kind = 'G';
                        {
                            int gid0 = gd0.getGadgetId();
                            if (gid0 == 70540003 || gid0 == 70540021) kind = 'O';
                            else if (gid0 >= 70200000 && gid0 < 70300000) kind = 'C';        // 宝箱 70211xxx
                            else if (gid0 >= 70500000 && gid0 < 70600000) kind = 'I';   // 采集物 705x
                            else if (gid0 >= 71700000 && gid0 < 71800000) kind = 'I';   // 采集物 717x
                            else if (gid0 >= 70360000 && gid0 < 70370000) kind = 'K';   // 机关/Gear
                        }
                    }
                    else if (e instanceof EntityItem) kind = 'I';
                    else if (e instanceof EntityAvatar) kind = 'A';
                    else kind = 'E';

                    double hp = e.hasFightProperty(FightProperty.FIGHT_PROP_CUR_HP)
                            ? e.getFightProperty(FightProperty.FIGHT_PROP_CUR_HP) : 0;
                    double maxHp = e.hasFightProperty(FightProperty.FIGHT_PROP_MAX_HP)
                            ? e.getFightProperty(FightProperty.FIGHT_PROP_MAX_HP) : 0;
                    int level = 0;
                    if (e instanceof EntityMonster m) {
                        try {
                            level = m.getLevel();
                        } catch (Throwable ignored) {
                        }
                    }

                    body.append("e ").append(kind).append(' ').append(e.getId()).append(' ').append(typeId)
                            .append(' ').append(fmt(ep.getX())).append(' ').append(fmt(ep.getY())).append(' ')
                            .append(fmt(ep.getZ())).append(' ').append(fmt(hp)).append(' ').append(fmt(maxHp))
                            .append(' ').append(level).append(' ').append(fmt(dist)).append('\n');
                    if (++n >= 400) break;
                }
                sb.append("ents ").append(n).append('\n').append(body);
            } else {
                sb.append("ents 0\n");
            }
        }

        sb.append("st")
                .append(" god=").append(god ? 1 : 0)
                .append(" fall=").append(fall ? 1 : 0)
                .append(" loot=").append(loot ? 1 : 0)
                .append(" stamina=").append(stamina ? 1 : 0)
                .append(" autoheal=").append(autoHeal ? 1 : 0)
                .append(" mult=").append(owner == null ? 1f : owner.mult)
                .append(" aura=").append(owner != null && owner.aura ? 1 : 0)
                .append(" aurar=").append(owner == null ? 12f : owner.auraRadius)
                .append(" auradmg=").append(owner == null ? 30000f : owner.auraDamage)
                .append(" lootr=").append(lootRadius)
                .append(" freeze=").append(timeFreeze ? 1 : 0)
                .append(" timeh=").append(timeH)
                .append(" timem=").append(timeM)
                .append(" weather=").append(weather)
                .append(" esp=").append(owner != null && owner.esp ? 1 : 0)
                .append('\n');

        sb.append("act ").append(clean(lastAction)).append(' ').append(lastActionAt).append('\n');
        sb.append("end\n");
        return sb.toString();
    }

    private static long START = System.currentTimeMillis();

    private static long lastStartMs() {
        return START;
    }

    // ---------------------------------------------------------------- helpers

    /**
     * Parameters arrive in the URI query. Older overlay builds put them in the POST body instead,
     * so fall back to the body when the URL carries nothing — keeps both sides working.
     */
    private static String rawQuery(HttpExchange ex) {
        String q = ex.getRequestURI().getRawQuery();
        if (q != null && !q.isEmpty()) return q;
        try {
            byte[] body = ex.getRequestBody().readAllBytes();
            return body.length == 0 ? "" : new String(body, StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
            return "";
        }
    }

    private static Map<String, String> parseQuery(String raw) {
        Map<String, String> out = new HashMap<>();
        if (raw == null || raw.isEmpty()) return out;
        for (String pair : raw.split("&")) {
            if (pair.isEmpty()) continue;
            int eq = pair.indexOf('=');
            String k = eq < 0 ? pair : pair.substring(0, eq);
            String v = eq < 0 ? "" : pair.substring(eq + 1);
            try {
                k = URLDecoder.decode(k, "UTF-8");
                v = URLDecoder.decode(v, "UTF-8");
            } catch (Throwable ignored) {
            }
            out.put(k, v);
        }
        return out;
    }

    private static void respond(HttpExchange ex, String text) {
        try {
            byte[] out = text.getBytes(StandardCharsets.UTF_8);
            ex.getResponseHeaders().add("Content-Type", "text/plain; charset=utf-8");
            ex.sendResponseHeaders(200, out.length);
            try (OutputStream os = ex.getResponseBody()) {
                os.write(out);
            }
        } catch (Throwable ignored) {
        }
    }

    private static boolean b(String v) {
        if (v == null) return false;
        String s = v.trim().toLowerCase(Locale.ROOT);
        return s.equals("1") || s.equals("true") || s.equals("on") || s.equals("yes");
    }

    private static float fl(String v, float d) {
        try {
            return Float.parseFloat(v.trim());
        } catch (Throwable t) {
            return d;
        }
    }

    private static float f(Map<String, String> q, String k, float d) {
        String v = q.get(k);
        return v == null ? d : fl(v, d);
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    private static String fmt(double v) {
        if (!Double.isFinite(v)) return "0";
        return String.format(Locale.ROOT, "%.2f", v);
    }

    private static String clean(String s) {
        if (s == null) return "-";
        return s.replace('\n', ' ').replace('\r', ' ').replace(' ', '_');
    }
}
