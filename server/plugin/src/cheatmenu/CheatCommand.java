package cheatmenu;

import emu.grasscutter.command.Command;
import emu.grasscutter.command.CommandHandler;
import emu.grasscutter.command.CommandOutput;
import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.world.Position;
import picocli.CommandLine;
import picocli.CommandLine.Parameters;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/** {@code /cheat} — the in-game menu. Same layout idea as Akebi's ImGui tabs. */
@Command(label = "cheat", aliases = {"cm"})
public final class CheatCommand implements CommandHandler {

    @Override
    public CommandLine createCommandLine(Player sender, Player targetPlayer) {
        Player who = sender != null ? sender : targetPlayer;

        CommandLine root = new CommandLine(new Status(who));
        root.addSubcommand("god", new Toggle(who, "god"));
        root.addSubcommand("fall", new Toggle(who, "fall"));
        root.addSubcommand("loot", new Toggle(who, "loot"));
        root.addSubcommand("stamina", new Toggle(who, "stamina"));
        root.addSubcommand("esp", new Esp(who));
        root.addSubcommand("mult", new Mult(who));
        root.addSubcommand("aura", new Aura(who));
        root.addSubcommand("killall", new KillAll(who));
        root.addSubcommand("tp", new Teleport(who));
        root.addSubcommand("save", new Save(who));
        root.addSubcommand("go", new Go(who));
        root.addSubcommand("chest", new Chest(who));
        root.addSubcommand("time", new Time(who));
        root.addSubcommand("weather", new Weather(who));
        root.addSubcommand("near", new Near(who));
        root.addSubcommand("lootnow", new LootNow(who));
        root.addSubcommand("dump", new Dump(who));
        root.addSubcommand("heal", new Heal(who));
        root.addSubcommand("help", new Help(who));
        return root;
    }

    private static CheatMenu plugin() {
        return CheatMenu.getInstance();
    }

    static void say(Player player, String message) {
        CommandOutput.sendMessage(player, message);
    }

    static boolean ready(Player player) {
        if (plugin() == null) {
            say(player, "[CheatMenu] plugin not initialised yet");
            return false;
        }
        if (player == null) {
            say(null, "[CheatMenu] run this in game (console has no player context)");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------ subcommands

    @CommandLine.Command(name = "cheat", description = "Akebi-style cheat menu (server side).")
    public static final class Status implements Runnable {
        private final Player player;

        public Status(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            Flags.PlayerFlags f = plugin().flags().of(this.player.getUid());
            say(this.player, """
                    [CheatMenu] uid=%d  scene=%s
                      god=%s  fall=%s  loot=%s  aura=%s(%.0fm)  mult=x%.2f  esp=%s
                      /cheat help 看全部命令""".formatted(
                    this.player.getUid(),
                    this.player.getScene() == null ? "-" : this.player.getScene().getId(),
                    f.god, f.fall, f.loot, plugin().aura, plugin().auraRadius,
                    plugin().mult, plugin().esp));
        }
    }

    @CommandLine.Command(name = "help", description = "list commands")
    public static final class Help implements Runnable {
        private final Player player;

        public Help(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            say(this.player, """
                    [CheatMenu] 命令
                      /cheat god|fall|loot|stamina [on|off|toggle]   开关
                      /cheat mult <x>            伤害倍率（1 = 原版，100 = 秒杀）
                      /cheat aura [on|off] [半径] 杀意光环：每 400ms 打周围所有怪
                      /cheat killall             清掉当前场景所有怪
                      /cheat tp <x> <y> <z>      传送
                      /cheat save <名字> / /cheat go <名字>   传送书签
                      /cheat chest               传送到最近的宝箱
                      /cheat time <时> [分]       改世界时间
                      /cheat weather <id>        改天气
                      /cheat near [半径]          列出附近实体（调参用）
                      /cheat lootnow | dump | heal
                      /cheat esp [on|off]        实体流开关（雷达用）""");
        }
    }

    @CommandLine.Command(name = "toggle", description = "on | off | toggle")
    public static final class Toggle implements Runnable {
        private final Player player;
        private final String feature;

        @Parameters(index = "0", arity = "0..1", description = "on | off | toggle")
        private String mode;

        public Toggle(Player player, String feature) {
            this.player = player;
            this.feature = feature;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            Flags.PlayerFlags f = plugin().flags().of(this.player.getUid());
            String value = "?";

            switch (this.feature) {
                case "god" -> {
                    f.god = Flags.apply(this.mode, f.god);
                    value = String.valueOf(f.god);
                }
                case "fall" -> {
                    f.fall = Flags.apply(this.mode, f.fall);
                    value = String.valueOf(f.fall);
                }
                case "loot" -> {
                    f.loot = Flags.apply(this.mode, f.loot);
                    value = String.valueOf(f.loot);
                    if (f.loot) plugin().vacuum(this.player, CheatMenu.LOOT_RADIUS);
                }
                case "stamina" -> {
                    boolean on = Flags.apply(this.mode, this.player.isUnlimitedStamina());
                    this.player.setUnlimitedStamina(on);
                    value = String.valueOf(on);
                }
                default -> {
                }
            }
            say(this.player, "[CheatMenu] " + this.feature + " = " + value);
        }
    }

    @CommandLine.Command(name = "esp", description = "on | off | toggle")
    public static final class Esp implements Runnable {
        private final Player player;

        @Parameters(index = "0", arity = "0..1", description = "on | off | toggle")
        private String mode;

        public Esp(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            plugin().esp = Flags.apply(this.mode, plugin().esp);
            say(this.player, "[CheatMenu] esp = " + plugin().esp + " -> " + Bridge.target());
        }
    }

    @CommandLine.Command(name = "mult", description = "damage multiplier")
    public static final class Mult implements Runnable {
        private final Player player;

        @Parameters(index = "0", arity = "0..1", description = "1..1000")
        private Float value;

        public Mult(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            if (this.value == null) {
                say(this.player, "[CheatMenu] mult = x" + plugin().mult);
                return;
            }
            plugin().mult = Math.max(0.1f, Math.min(1000f, this.value));
            say(this.player, "[CheatMenu] mult = x" + plugin().mult);
        }
    }

    @CommandLine.Command(name = "aura", description = "kill aura on|off [radius]")
    public static final class Aura implements Runnable {
        private final Player player;

        @Parameters(index = "0", arity = "0..1", description = "on | off | toggle")
        private String mode;

        @Parameters(index = "1", arity = "0..1", description = "radius in meters")
        private Float radius;

        public Aura(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            if (this.radius != null) {
                plugin().auraRadius = Math.max(1f, Math.min(100f, this.radius));
            }
            plugin().aura = Flags.apply(this.mode, plugin().aura);
            say(this.player, "[CheatMenu] aura = " + plugin().aura
                    + "  radius = " + plugin().auraRadius + "m");
        }
    }

    @CommandLine.Command(name = "killall", description = "kill every monster in the scene")
    public static final class KillAll implements Runnable {
        private final Player player;

        public KillAll(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            int killed = plugin().killAll(this.player);
            say(this.player, "[CheatMenu] killall -> " + killed + " monster(s)");
        }
    }

    @CommandLine.Command(name = "tp", description = "teleport to x y z")
    public static final class Teleport implements Runnable {
        private final Player player;

        @Parameters(index = "0") private float x;
        @Parameters(index = "1") private float y;
        @Parameters(index = "2") private float z;

        public Teleport(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            try {
                this.player.teleport(new Position(this.x, this.y, this.z));
                say(this.player, "[CheatMenu] tp -> %.1f, %.1f, %.1f".formatted(this.x, this.y, this.z));
            } catch (Throwable t) {
                say(this.player, "[CheatMenu] tp failed: " + t);
            }
        }
    }

    @CommandLine.Command(name = "save", description = "save current position as a bookmark")
    public static final class Save implements Runnable {
        private final Player player;

        @Parameters(index = "0", description = "bookmark name")
        private String name;

        public Save(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            Position pos = this.player.getPosition();
            if (pos == null) return;
            Remote.saveBookmark(this.name, pos);
            say(this.player, "[CheatMenu] saved '%s' at %.1f, %.1f, %.1f"
                    .formatted(this.name, pos.getX(), pos.getY(), pos.getZ()));
        }
    }

    @CommandLine.Command(name = "go", description = "teleport to a bookmark")
    public static final class Go implements Runnable {
        private final Player player;

        @Parameters(index = "0", arity = "0..1", description = "bookmark name (empty = list)")
        private String name;

        public Go(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            if (this.name == null) {
                say(this.player, "[CheatMenu] bookmarks: " + String.join(", ", Remote.listBookmarks()));
                return;
            }
            Position pos = Remote.loadBookmark(this.name);
            if (pos == null) {
                say(this.player, "[CheatMenu] no bookmark '" + this.name + "'");
                return;
            }
            this.player.teleport(pos);
            say(this.player, "[CheatMenu] go '" + this.name + "'");
        }
    }

    @CommandLine.Command(name = "chest", description = "teleport to the nearest chest")
    public static final class Chest implements Runnable {
        private final Player player;

        public Chest(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            EntityGadget chest = plugin().nearestChest(this.player);
            if (chest == null) {
                say(this.player, "[CheatMenu] 附近没有找到宝箱类 gadget（用 /cheat near 看实际 id）");
                return;
            }
            Position pos = chest.getPosition();
            this.player.teleport(new Position(pos.getX(), pos.getY() + 1.0f, pos.getZ()));
            say(this.player, "[CheatMenu] chest %d at %.1f, %.1f, %.1f".formatted(
                    chest.getGadgetId(), pos.getX(), pos.getY(), pos.getZ()));
        }
    }

    @CommandLine.Command(name = "time", description = "set world time")
    public static final class Time implements Runnable {
        private final Player player;

        @Parameters(index = "0", description = "hour 0-23")
        private int hour;

        @Parameters(index = "1", arity = "0..1", description = "minute 0-59")
        private Integer minute;

        public Time(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            try {
                this.player.getWorld().changeTime(this.hour, this.minute == null ? 0 : this.minute);
                say(this.player, "[CheatMenu] time -> %02d:%02d"
                        .formatted(this.hour, this.minute == null ? 0 : this.minute));
            } catch (Throwable t) {
                say(this.player, "[CheatMenu] time failed: " + t);
            }
        }
    }

    @CommandLine.Command(name = "weather", description = "set weather by id")
    public static final class Weather implements Runnable {
        private final Player player;

        @Parameters(index = "0", description = "weather id")
        private int id;

        public Weather(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            try {
                this.player.setWeather(this.id);
                say(this.player, "[CheatMenu] weather -> " + this.id);
            } catch (Throwable t) {
                say(this.player, "[CheatMenu] weather failed: " + t);
            }
        }
    }

    @CommandLine.Command(name = "near", description = "list nearby entities")
    public static final class Near implements Runnable {
        private final Player player;

        @Parameters(index = "0", arity = "0..1", description = "radius in meters (default 40)")
        private Float radius;

        public Near(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            double r = this.radius == null ? 40.0 : Math.max(1.0, this.radius);
            Position origin = this.player.getPosition();
            if (origin == null || this.player.getScene() == null) return;

            List<GameEntity> list = new ArrayList<>(this.player.getScene().getEntities().values());
            list.sort(Comparator.comparingDouble(e -> CheatMenu.distance2(origin, e.getPosition())));

            StringBuilder sb = new StringBuilder("[CheatMenu] 附近 %.0fm：".formatted(r));
            int shown = 0;
            for (GameEntity e : list) {
                double d2 = CheatMenu.distance2(origin, e.getPosition());
                if (d2 > r * r) break;
                if (e instanceof EntityMonster m) {
                    sb.append("\n  %.1fm 怪物 id=%d mob=%d lvl=%d hp=%.0f".formatted(
                            Math.sqrt(d2), e.getId(), m.getMonsterData() == null ? 0 : m.getMonsterData().getId(),
                            m.getLevel(), m.getFightProperty(emu.grasscutter.game.props.FightProperty.FIGHT_PROP_CUR_HP)));
                } else if (e instanceof EntityGadget g) {
                    sb.append("\n  %.1fm 机关 id=%d gadget=%d".formatted(Math.sqrt(d2), e.getId(), g.getGadgetId()));
                } else {
                    sb.append("\n  %.1fm %s id=%d".formatted(Math.sqrt(d2), e.getEntityType(), e.getId()));
                }
                if (++shown >= 20) {
                    sb.append("\n  ...（只列前 20 个）");
                    break;
                }
            }
            if (shown == 0) sb.append("\n  （空）");
            say(this.player, sb.toString());
        }
    }

    @CommandLine.Command(name = "lootnow", description = "vacuum drops once")
    public static final class LootNow implements Runnable {
        private final Player player;

        public LootNow(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            int moved = plugin().vacuum(this.player, CheatMenu.LOOT_RADIUS);
            say(this.player, "[CheatMenu] vacuum picked up " + moved + " item(s)");
        }
    }

    @CommandLine.Command(name = "dump", description = "write entities.json now")
    public static final class Dump implements Runnable {
        private final Player player;

        public Dump(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            int count = Bridge.write(plugin().getServer());
            say(this.player, "[CheatMenu] wrote " + count + " entities -> " + Bridge.target());
        }
    }

    @CommandLine.Command(name = "heal", description = "heal to max HP")
    public static final class Heal implements Runnable {
        private final Player player;

        public Heal(Player player) {
            this.player = player;
        }

        @Override
        public void run() {
            if (!ready(this.player)) return;
            try {
                this.player.setHealth(this.player.getMaxHealth());
                say(this.player, "[CheatMenu] healed to %.0f".formatted(this.player.getMaxHealth()));
            } catch (Throwable t) {
                say(this.player, "[CheatMenu] heal failed: " + t);
            }
        }
    }

    static String fmt(String pattern, Object... args) {
        return String.format(Locale.ROOT, pattern, args);
    }
}
