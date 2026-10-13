package emu.grasscutter.game.reward;

import emu.grasscutter.data.common.ItemParamData;
import emu.grasscutter.data.common.PointData;
import org.tomlj.Toml;
import org.tomlj.TomlArray;
import org.tomlj.TomlParseResult;
import org.tomlj.TomlTable;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Server-owned, first-unlock rewards. The native TransPointRewardConfigData table is unused. */
public final class TransPointRewardConfig {
    public enum Kind { WAYPOINT, STATUE, DOMAIN }

    private static final String TEMPLATE = "/trans-point-rewards.toml";
    private static final int FORMAT_VERSION = 1;
    private static final Set<String> REWARD_KEYS =
            Set.of("adventureExp", "primogems", "mora", "items");
    private static volatile TransPointRewardConfig active;

    private final Map<Kind, List<ItemParamData>> kinds;
    private final Map<Integer, List<ItemParamData>> scenes;
    private final Map<Long, List<ItemParamData>> points;

    private TransPointRewardConfig(
            Map<Kind, List<ItemParamData>> kinds,
            Map<Integer, List<ItemParamData>> scenes,
            Map<Long, List<ItemParamData>> points) {
        this.kinds = Map.copyOf(kinds);
        this.scenes = Map.copyOf(scenes);
        this.points = Map.copyOf(points);
    }

    public static void loadOrCreate(Path path) {
        try {
            Path legacy = Path.of("trans-point-rewards.toml");
            if (Files.exists(legacy) && Files.exists(path)) {
                throw new IllegalStateException(
                        "Both " + legacy + " and " + path
                                + " exist. Remove the obsolete copy explicitly to avoid ambiguous rewards.");
            }
            if (Files.exists(legacy)) {
                Files.createDirectories(path.toAbsolutePath().getParent());
                Files.move(legacy, path);
            } else if (Files.notExists(path)) {
                Files.createDirectories(path.toAbsolutePath().getParent());
                try (InputStream source = TransPointRewardConfig.class.getResourceAsStream(TEMPLATE)) {
                    if (source == null) throw new IOException("Missing bundled template " + TEMPLATE);
                    Files.copy(source, path);
                }
            }
            // Failed edits must never silently reset the owner's configuration.
            active = parse(Files.readString(path));
        } catch (IOException | IllegalArgumentException e) {
            throw new IllegalStateException("Cannot load " + path + ": " + e.getMessage(), e);
        }
    }

    public static TransPointRewardConfig current() {
        var value = active;
        if (value == null) throw new IllegalStateException("trans-point-rewards.toml was not loaded");
        return value;
    }

    public static TransPointRewardConfig parse(String text) {
        TomlParseResult root = Toml.parse(text);
        if (root.hasErrors()) throw new IllegalArgumentException("Invalid TOML: " + root.errors());

        Object version = root.get("version");
        if (!(version instanceof Long n) || n != FORMAT_VERSION) {
            throw new IllegalArgumentException("Expected version = " + FORMAT_VERSION);
        }

        var kinds = new HashMap<Kind, List<ItemParamData>>();
        var scenes = new HashMap<Integer, List<ItemParamData>>();
        var points = new HashMap<Long, List<ItemParamData>>();
        for (var entry : root.entrySet()) {
            String selector = entry.getKey();
            if ("version".equals(selector)) continue;
            if (!(entry.getValue() instanceof TomlTable table)) {
                throw new IllegalArgumentException("Expected reward table for [" + selector + "]");
            }
            List<ItemParamData> reward = reward(selector, table);
            if (selector.startsWith("kind=")) {
                Kind kind = switch (selector.substring(5)) {
                    case "waypoint" -> Kind.WAYPOINT;
                    case "statue" -> Kind.STATUE;
                    case "domain" -> Kind.DOMAIN;
                    default -> throw new IllegalArgumentException("Unknown selector: " + selector);
                };
                kinds.put(kind, reward);
                continue;
            }
            if (!selector.matches("[1-9][0-9]*:([1-9][0-9]*|\\*)")) {
                throw new IllegalArgumentException("Unsupported selector: " + selector);
            }
            String[] pair = selector.split(":", 2);
            int sceneId;
            try {
                sceneId = Integer.parseInt(pair[0]);
                if ("*".equals(pair[1])) {
                    scenes.put(sceneId, reward);
                } else {
                    int pointId = Integer.parseInt(pair[1]);
                    points.put(key(sceneId, pointId), reward);
                }
            } catch (NumberFormatException ex) {
                throw new IllegalArgumentException("Selector ID is too large: " + selector, ex);
            }
        }
        for (Kind kind : Kind.values()) {
            if (!kinds.containsKey(kind)) {
                throw new IllegalArgumentException("Missing [\"kind="
                        + kind.name().toLowerCase(java.util.Locale.ROOT) + "\"]");
            }
        }
        return new TransPointRewardConfig(kinds, scenes, points);
    }

    private static List<ItemParamData> reward(String name, TomlTable table) {
        for (String field : table.keySet()) {
            if (!REWARD_KEYS.contains(field)) {
                throw new IllegalArgumentException("Unknown reward field " + field + " in " + name);
            }
        }
        var result = new ArrayList<ItemParamData>();
        add(result, 102, number(table, "adventureExp", name, false));
        add(result, 201, number(table, "primogems", name, false));
        add(result, 202, number(table, "mora", name, false));
        Object rawItems = table.get("items");
        if (rawItems != null) {
            if (!(rawItems instanceof TomlArray items)) {
                throw new IllegalArgumentException(name + ".items must be an array");
            }
            for (int i = 0; i < items.size(); i++) {
                if (!(items.get(i) instanceof TomlTable item)) {
                    throw new IllegalArgumentException(name + ".items[" + i + "] must be a table");
                }
                if (!item.keySet().equals(Set.of("id", "count"))) {
                    throw new IllegalArgumentException(name + ".items[" + i + "] needs id and count");
                }
                int id = number(item, "id", name, true);
                int count = number(item, "count", name, true);
                result.add(new ItemParamData(id, count));
            }
        }
        return List.copyOf(result);
    }

    private static int number(TomlTable table, String field, String name, boolean positive) {
        Object value = table.get(field);
        if (value == null && !positive) return 0;
        if (!(value instanceof Long n) || n > Integer.MAX_VALUE
                || (positive ? n <= 0 : n < 0)) {
            throw new IllegalArgumentException(name + "." + field + " must be a "
                    + (positive ? "positive" : "non-negative") + " integer");
        }
        return n.intValue();
    }

    private static void add(List<ItemParamData> items, int id, int count) {
        if (count > 0) items.add(new ItemParamData(id, count));
    }

    private static long key(int sceneId, int pointId) {
        return ((long) sceneId << 32) | (pointId & 0xffffffffL);
    }

    /** Exact point > scene > kind. A selected table replaces the entire reward. */
    public List<ItemParamData> resolve(int sceneId, int pointId, Kind kind) {
        var exact = points.get(key(sceneId, pointId));
        if (exact != null) return exact;
        var scene = scenes.get(sceneId);
        if (scene != null) return scene;
        return kinds.getOrDefault(kind, List.of());
    }

    /** Point subtype and native pre-unlock flags are eligibility, not reward configuration. */
    public static Kind kindOf(PointData point, boolean isStatue) {
        if (point == null || point.isUnlocked()) return null;
        // Recent statue resources may use an obfuscated $type rather than SceneTransPoint.
        if (isStatue && emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(point)) {
            return Kind.STATUE;
        }
        if (!"SceneTransPoint".equals(point.getType())) return null;
        // PORTAL and unidentified TOWER subtypes are not ordinary waypoints.
        if (point.getPointType() != null) return null;
        if ("Never".equals(point.getMapVisibility())) return null;
        return Kind.WAYPOINT;
    }
}
