package cheatmenu;

import emu.grasscutter.net.packet.BasePacket;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.SceneAreaWeatherNotifyOuterClass.SceneAreaWeatherNotify;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import emu.grasscutter.Grasscutter;
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

    private static long lastLootMs = 0L;
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
                // The menu's 1/2/3 are climate slots (sunny / cloudy / rain), NOT weather areas.
                // Areas 1..3 are Mondstadt's storm-gadget areas: changing to them makes the server
                // send a gadget id the client then has to mount, and that mount is what crashed the
                // client. Send a climate-only notify with no weather area and no gadget instead.
                int climate = (int) f(q, "id", 1);
                if (actor.getWorld() != null && actor.getWorld().isTimeLocked())
                    actor.getWorld().lockTime(false);
                var notify = SceneAreaWeatherNotify.newBuilder()
                        .setWeatherAreaId(0)
                        .setClimateType(climate)
                        .build();
                BasePacket packet = new BasePacket(PacketOpcodes.SceneAreaWeatherNotify);
                packet.setData(notify);
                actor.sendPacket(packet);
                weather = climate;
                lastAction = "climate: " + climate;
            }
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
                    else if (e instanceof EntityGadget) kind = 'G';
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
