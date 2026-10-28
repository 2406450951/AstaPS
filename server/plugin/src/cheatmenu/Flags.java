package cheatmenu;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** Per-player toggle state. Keyed by UID, created on first touch. */
public final class Flags {

    public static final class PlayerFlags {
        public volatile boolean god;
        public volatile boolean fall;
        public volatile boolean loot;
    }

    private final Map<Integer, PlayerFlags> flags = new ConcurrentHashMap<>();

    public PlayerFlags of(int uid) {
        return this.flags.computeIfAbsent(uid, uid2 -> new PlayerFlags());
    }

    public void drop(int uid) {
        this.flags.remove(uid);
    }

    /** Applies "on" / "off" / "toggle" / empty(=toggle) to a flag, returns the new value. */
    public static boolean apply(String action, boolean current) {
        if (action == null || action.isEmpty() || action.equalsIgnoreCase("toggle")) return !current;
        return switch (action.toLowerCase()) {
            case "on", "true", "1", "yes" -> true;
            case "off", "false", "0", "no" -> false;
            default -> !current;
        };
    }
}
