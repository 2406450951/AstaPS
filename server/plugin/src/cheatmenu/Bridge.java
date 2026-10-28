package cheatmenu;

import emu.grasscutter.game.entity.EntityAvatar;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.props.EntityType;
import emu.grasscutter.game.props.FightProperty;
import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.Scene;
import emu.grasscutter.game.world.World;
import emu.grasscutter.server.game.GameServer;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * Entity feed: snapshots every scene entity (position / hp / type) to
 * {@code data/entities.json} so an external overlay can draw a radar or a real ESP.
 *
 * <p>This is the M5 data bridge from PLAN-71-SOLO. It needs no client patch and no injection —
 * the server already knows where everything is.
 */
public final class Bridge {

    private Bridge() {}

    private static final Path DIR = Path.of("data");
    private static final Path OUT = DIR.resolve("entities.json");
    private static final Path TMP = DIR.resolve("entities.json.tmp");

    public static Path target() {
        return OUT.toAbsolutePath();
    }

    /** Number of entities written by the last call. */
    public static volatile int lastCount = 0;

    public static int write(GameServer server) {
        if (server == null) return 0;

        StringBuilder sb = new StringBuilder(1 << 16);
        sb.append("{\"ts\":").append(System.currentTimeMillis());
        sb.append(",\"tick\":").append(server.getCompletedTickCount());

        // ---- players (radar origin / camera-less ESP) ----
        sb.append(",\"players\":[");
        boolean first = true;
        for (Player player : server.getPlayers().values()) {
            if (player == null || player.getScene() == null) continue;
            if (!first) sb.append(',');
            first = false;
            Position p = player.getPosition();
            Position r = player.getRotation();
            sb.append("{\"uid\":").append(player.getUid());
            sb.append(",\"nick\":\"").append(escape(player.getNickname())).append('"');
            sb.append(",\"scene\":").append(player.getScene().getId());
            sb.append(",\"x\":").append(num(p == null ? 0 : p.getX()));
            sb.append(",\"y\":").append(num(p == null ? 0 : p.getY()));
            sb.append(",\"z\":").append(num(p == null ? 0 : p.getZ()));
            sb.append(",\"rx\":").append(num(r == null ? 0 : r.getX()));
            sb.append(",\"ry\":").append(num(r == null ? 0 : r.getY()));
            sb.append(",\"rz\":").append(num(r == null ? 0 : r.getZ()));
            sb.append('}');
        }
        sb.append(']');

        // ---- entities ----
        sb.append(",\"entities\":[");
        first = true;
        int count = 0;
        for (World world : server.getWorlds()) {
            if (world == null || world.getScenes() == null) continue;
            for (Scene scene : world.getScenes().values()) {
                if (scene == null || scene.getEntities() == null) continue;
                for (GameEntity entity : scene.getEntities().values()) {
                    if (entity == null) continue;
                    Position pos = entity.getPosition();
                    if (pos == null) continue;
                    if (!first) sb.append(',');
                    first = false;
                    count++;

                    EntityType type = entity.getEntityType();
                    sb.append("{\"id\":").append(entity.getId());
                    sb.append(",\"kind\":\"").append(type == null ? "?" : type.name()).append('"');
                    sb.append(",\"typeId\":").append(entity.getEntityTypeId());
                    sb.append(",\"scene\":").append(scene.getId());
                    sb.append(",\"x\":").append(num(pos.getX()));
                    sb.append(",\"y\":").append(num(pos.getY()));
                    sb.append(",\"z\":").append(num(pos.getZ()));

                    if (entity.hasFightProperty(FightProperty.FIGHT_PROP_CUR_HP)) {
                        sb.append(",\"hp\":").append(num(entity.getFightProperty(FightProperty.FIGHT_PROP_CUR_HP)));
                        sb.append(",\"maxHp\":").append(num(entity.getFightProperty(FightProperty.FIGHT_PROP_MAX_HP)));
                    }
                    if (entity instanceof EntityMonster monster) {
                        sb.append(",\"lvl\":").append(monster.getLevel());
                        try {
                            if (monster.getMonsterData() != null) {
                                sb.append(",\"mob\":").append(monster.getMonsterData().getId());
                            }
                        } catch (Throwable ignored) {
                            // monster data missing for scripted spawns; level is enough
                        }
                    } else if (entity instanceof EntityAvatar avatar) {
                        Player owner = avatar.getPlayer();
                        if (owner != null) sb.append(",\"uid\":").append(owner.getUid());
                        sb.append(",\"self\":true");
                    }
                    sb.append('}');
                }
            }
        }
        sb.append("]}");

        lastCount = count;
        try {
            Files.createDirectories(DIR);
            Files.writeString(TMP, sb.toString(), StandardCharsets.UTF_8);
            try {
                Files.move(TMP, OUT, StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE);
            } catch (Throwable atomicUnsupported) {
                Files.move(TMP, OUT, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (Throwable t) {
            CheatMenu.getInstance().getLogger().warn("[CheatMenu] entity feed write failed: {}", t.toString());
        }
        return count;
    }

    private static String num(double v) {
        if (!Double.isFinite(v)) return "0";
        return String.format(java.util.Locale.ROOT, "%.2f", v);
    }

    private static String escape(String s) {
        if (s == null) return "";
        StringBuilder out = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> out.append("\\\"");
                case '\\' -> out.append("\\\\");
                case '\n' -> out.append("\\n");
                case '\r' -> out.append("\\r");
                case '\t' -> out.append("\\t");
                default -> {
                    if (c < 0x20) out.append(String.format("\\u%04x", (int) c));
                    else out.append(c);
                }
            }
        }
        return out.toString();
    }
}
