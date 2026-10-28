package cheatmenu;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.world.Position;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

/**
 * File-based control channel between the CheatMenu plugin and the external panel.
 *
 * <p>The game has no in-process way for an outside program to press its buttons, and injecting
 * a DLL is not required for any of this: the panel writes two small files, the plugin polls them.
 *
 * <pre>
 *   data\cheat-state.json    panel -> plugin   {"god":true,"loot":true,"mult":2.0,"aura":false}
 *   data\cheat-cmd.txt       panel -> plugin   one command per line, executed as the first online player
 *   data\cheat-status.json   plugin -> panel   live state + player position
 *   data\cheat-bookmarks.json panel <-> plugin {"home":[x,y,z]}
 * </pre>
 */
public final class Remote {

    private Remote() {}

    public static final Path DIR = Path.of("data");
    public static final Path STATE = DIR.resolve("cheat-state.json");
    public static final Path CMD = DIR.resolve("cheat-cmd.txt");
    public static final Path STATUS = DIR.resolve("cheat-status.json");
    public static final Path BOOKMARKS = DIR.resolve("cheat-bookmarks.json");

    private static long stateMtime = -1L;
    private static long statusMtime = 0L;

    /** Flat JSON object reader — enough for the hand-written state file, no dependency needed. */
    public static Map<String, String> readFlat(Path path) {
        Map<String, String> out = new LinkedHashMap<>();
        try {
            if (!Files.exists(path)) return out;
            String text = Files.readString(path, StandardCharsets.UTF_8).trim();
            int open = text.indexOf('{');
            int close = text.lastIndexOf('}');
            if (open < 0 || close <= open) return out;
            text = text.substring(open + 1, close);
            for (String pair : splitTopLevel(text)) {
                int colon = pair.indexOf(':');
                if (colon < 0) continue;
                String key = pair.substring(0, colon).trim().replace("\"", "").replace("'", "");
                String value = pair.substring(colon + 1).trim().replace("\"", "").replace("'", "");
                if (!key.isEmpty()) out.put(key.toLowerCase(Locale.ROOT), value);
            }
        } catch (Throwable ignored) {
            // a torn read just means "no change this tick"
        }
        return out;
    }

    private static List<String> splitTopLevel(String text) {
        List<String> parts = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        int depth = 0;
        boolean inString = false;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '"') inString = !inString;
            if (!inString) {
                if (c == '{' || c == '[') depth++;
                if (c == '}' || c == ']') depth--;
                if (c == ',' && depth == 0) {
                    parts.add(cur.toString());
                    cur.setLength(0);
                    continue;
                }
            }
            cur.append(c);
        }
        if (cur.length() > 0) parts.add(cur.toString());
        return parts;
    }

    public static boolean getBool(Map<String, String> m, String key, boolean fallback) {
        String v = m.get(key.toLowerCase(Locale.ROOT));
        if (v == null) return fallback;
        return switch (v.toLowerCase(Locale.ROOT)) {
            case "true", "on", "1", "yes" -> true;
            case "false", "off", "0", "no" -> false;
            default -> fallback;
        };
    }

    public static float getFloat(Map<String, String> m, String key, float fallback) {
        String v = m.get(key.toLowerCase(Locale.ROOT));
        if (v == null) return fallback;
        try {
            return Float.parseFloat(v);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** Applies the panel's state file to the plugin, only when the file actually changed. */
    public static void syncState(CheatMenu plugin) {
        try {
            if (!Files.exists(STATE)) return;
            long mtime = Files.getLastModifiedTime(STATE).toMillis();
            if (mtime == stateMtime) return;
            stateMtime = mtime;

            Map<String, String> m = readFlat(STATE);
            if (m.isEmpty()) return;

            plugin.esp = getBool(m, "esp", plugin.esp);
            plugin.mult = Math.max(0.1f, Math.min(10000f, getFloat(m, "mult", plugin.mult)));
            plugin.aura = getBool(m, "aura", plugin.aura);
            plugin.auraRadius = Math.max(1f, Math.min(100f, getFloat(m, "auraRadius", plugin.auraRadius)));

            boolean god = getBool(m, "god", false);
            boolean fall = getBool(m, "fall", false);
            boolean loot = getBool(m, "loot", false);
            for (Player player : plugin.onlinePlayers()) {
                Flags.PlayerFlags f = plugin.flags().of(player.getUid());
                f.god = god;
                f.fall = fall;
                f.loot = loot;
                if (getBool(m, "stamina", false)) player.setUnlimitedStamina(true);
            }
            plugin.getLogger().debug("[CheatMenu] state synced from panel");
        } catch (Throwable t) {
            plugin.getLogger().debug("[CheatMenu] state sync failed: {}", t.toString());
        }
    }

    /** Executes and removes the panel's queued commands (as the first online player). */
    public static int drainCommands(CheatMenu plugin) {
        try {
            if (!Files.exists(CMD)) return 0;
            List<String> lines = Files.readAllLines(CMD, StandardCharsets.UTF_8);
            if (lines.isEmpty()) {
                Files.deleteIfExists(CMD);
                return 0;
            }
            Player actor = plugin.onlinePlayers().stream().findFirst().orElse(null);
            int ran = 0;
            for (String raw : lines) {
                String line = raw == null ? "" : raw.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                if (line.startsWith("/")) line = line.substring(1);
                try {
                    Grasscutter.getCommandMap().invoke(actor, actor, line);
                    ran++;
                } catch (Throwable t) {
                    plugin.getLogger().warn("[CheatMenu] queued command failed: {} ({})", line, t.toString());
                }
            }
            Files.deleteIfExists(CMD);
            return ran;
        } catch (Throwable t) {
            plugin.getLogger().debug("[CheatMenu] command drain failed: {}", t.toString());
            return 0;
        }
    }

    /** Publishes plugin state for the panel to render. Cheap enough to run once a second. */
    public static void writeStatus(CheatMenu plugin, Player player) {
        try {
            long now = System.currentTimeMillis();
            if (now - statusMtime < 500L) return;
            statusMtime = now;

            Files.createDirectories(DIR);
            StringBuilder sb = new StringBuilder(512);
            sb.append("{\"ts\":").append(now);
            sb.append(",\"players\":").append(plugin.onlinePlayers().size());
            sb.append(",\"esp\":").append(plugin.esp);
            sb.append(",\"mult\":").append(plugin.mult);
            sb.append(",\"aura\":").append(plugin.aura);
            sb.append(",\"auraRadius\":").append(plugin.auraRadius);
            sb.append(",\"entities\":").append(Bridge.lastCount);
            if (player != null) {
                Flags.PlayerFlags f = plugin.flags().of(player.getUid());
                Position p = player.getPosition();
                sb.append(",\"uid\":").append(player.getUid());
                sb.append(",\"god\":").append(f.god);
                sb.append(",\"fall\":").append(f.fall);
                sb.append(",\"loot\":").append(f.loot);
                sb.append(",\"scene\":").append(player.getScene() == null ? 0 : player.getScene().getId());
                if (p != null) {
                    sb.append(",\"x\":").append(p.getX());
                    sb.append(",\"y\":").append(p.getY());
                    sb.append(",\"z\":").append(p.getZ());
                }
            }
            sb.append('}');
            Path tmp = DIR.resolve("cheat-status.json.tmp");
            Files.writeString(tmp, sb.toString(), StandardCharsets.UTF_8);
            Files.move(tmp, STATUS, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        } catch (Throwable ignored) {
            // status is cosmetic; never let it break a tick
        }
    }

    // ------------------------------------------------------------ bookmarks

    public static void saveBookmark(String name, Position pos) {
        try {
            Map<String, String> book = readFlat(BOOKMARKS);
            book.put(name.toLowerCase(Locale.ROOT),
                    String.format(Locale.ROOT, "%s,%s,%s", pos.getX(), pos.getY(), pos.getZ()));
            StringBuilder sb = new StringBuilder("{");
            boolean first = true;
            for (var e : book.entrySet()) {
                if (!first) sb.append(',');
                first = false;
                sb.append('"').append(e.getKey()).append("\":\"").append(e.getValue()).append('"');
            }
            sb.append('}');
            Files.createDirectories(DIR);
            Files.writeString(BOOKMARKS, sb.toString(), StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
        }
    }

    public static Position loadBookmark(String name) {
        String v = readFlat(BOOKMARKS).get(name.toLowerCase(Locale.ROOT));
        if (v == null) return null;
        String[] parts = v.split(",");
        if (parts.length != 3) return null;
        try {
            return new Position(Float.parseFloat(parts[0]), Float.parseFloat(parts[1]),
                    Float.parseFloat(parts[2]));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public static List<String> listBookmarks() {
        return new ArrayList<>(readFlat(BOOKMARKS).keySet());
    }
}
