package cheatmenu;

import emu.grasscutter.game.props.ActionReason;
import emu.grasscutter.server.packet.send.PacketItemAddHintNotify;
import emu.grasscutter.net.proto.VectorOuterClass.Vector;
import emu.grasscutter.net.packet.*;
import emu.grasscutter.net.proto.MotionInfoOuterClass.MotionInfo;
import emu.grasscutter.net.proto.MotionStateOuterClass.MotionState;
import emu.grasscutter.net.proto.EntityMoveInfoOuterClass.EntityMoveInfo;
import emu.grasscutter.server.packet.send.PacketSceneEntityMoveNotify;
import emu.grasscutter.server.packet.send.PacketAvatarFightPropUpdateNotify;
import emu.grasscutter.server.packet.send.PacketCanUseSkillNotify;
import emu.grasscutter.game.entity.EntityAvatar;
import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.EntityItem;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.inventory.GameItem;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.props.ElementType;
import emu.grasscutter.game.props.FightProperty;
import emu.grasscutter.game.world.Position;
import emu.grasscutter.game.world.Scene;
import emu.grasscutter.net.proto.VisionTypeOuterClass.VisionType;
import emu.grasscutter.plugin.Plugin;
import emu.grasscutter.plugin.api.ServerHelper;
import emu.grasscutter.server.event.EventHandler;
import emu.grasscutter.server.event.HandlerPriority;
import emu.grasscutter.server.event.entity.EntityDamageEvent;
import emu.grasscutter.server.event.entity.EntityDeathEvent;
import emu.grasscutter.server.event.game.ServerTickEvent;
import emu.grasscutter.server.event.player.PlayerJoinEvent;
import emu.grasscutter.server.event.player.PlayerQuitEvent;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;




/**
 * CheatMenu — the server-side half of Akebi-GC.
 *
 * <p>Server logic is authoritative for damage, drops, HP, time and weather, so every feature that
 * "settles a number" belongs here instead of in an injected DLL. Nothing in this class needs the
 * client patch; the client only has to be able to log in.
 *
 * <pre>
 *   god       GodMode       cancel EntityDamageEvent on our avatars + 500ms HP lock
 *   fall      FallControl   same, only for damage with no attacker (environment)
 *   loot      AutoLoot      vacuum EntityItem drops into the inventory
 *   aura      KillAura      damage every monster around the player each tick
 *   mult      Damage x N    scale damage the player deals
 *   esp       EntityFeed    data/entities.json stream for the radar / ESP
 * </pre>
 */
public final class CheatMenu extends Plugin {

    private static CheatMenu instance;

    private final Flags flags = new Flags();

    public volatile boolean esp = true;
    public volatile float mult = 1.0f;
    public volatile boolean aura = false;
    public volatile float auraRadius = 12.0f;
    public volatile float auraDamage = 30000.0f;

    private int tickCounter;
    private long lastHealMs;

    public static final double LOOT_RADIUS = 25.0;

    public static CheatMenu getInstance() {
        return instance;
    }

    public Flags flags() {
        return this.flags;
    }

    @Override
    public void onLoad() {
        instance = this;
        this.getLogger().info("[CheatMenu] loading (plugin api 2)");
    }

    @Override
    public void onEnable() {
        EventHandler.newHandler(this, EntityDamageEvent.class, this::onDamage, HandlerPriority.HIGH);
        EventHandler.newHandler(this, EntityDeathEvent.class, this::onDeath, HandlerPriority.NORMAL);
        EventHandler.newHandler(this, ServerTickEvent.class, this::onTick, HandlerPriority.NORMAL);
        EventHandler.newHandler(this, PlayerJoinEvent.class, this::onJoin, HandlerPriority.NORMAL);
        EventHandler.newHandler(this, PlayerQuitEvent.class, this::onQuit, HandlerPriority.NORMAL);

        try {
            ServerHelper.getInstance().registerCommand(new CheatCommand());
            this.getLogger().info("[CheatMenu] command registered: /cheat (alias /cm)");
        } catch (Throwable t) {
            this.getLogger().error("[CheatMenu] command registration failed", t);
        }

        CheatApi.start(this);

        try {
            ServerHelper.getInstance().addRouter(SdkRouter.class);
        } catch (Throwable t) {
            this.getLogger().error("[CheatMenu] SDK router registration failed", t);
        }

        this.getLogger().info("[CheatMenu] enabled — god/fall/loot/aura/mult/esp/tp/chest/time/weather");
    }

    @Override
    public void onDisable() {
        CheatApi.stop();
        this.getLogger().info("[CheatMenu] disabled");
    }

    // ---------------------------------------------------------------- events

    private void onDamage(EntityDamageEvent event) {
        try {
            GameEntity damager = event.getDamager();

            // Damage the player deals: scale it (Akebi's "Damage x N" / one-hit).
            if (this.mult != 1.0f && damager instanceof EntityAvatar attacker
                    && attacker.getPlayer() != null) {
                event.setDamage(event.getDamage() * this.mult);
            }

            if (!(event.getEntity() instanceof EntityAvatar avatar)) return;
            Player player = avatar.getPlayer();
            if (player == null) return;

            Flags.PlayerFlags f = this.flags.of(player.getUid());
            if (f.god) {
                event.cancel();
                return;
            }
            if (f.fall && damager == null) {
                event.cancel();
            }
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] onDamage failed: {}", t.toString());
        }
    }

    private void onDeath(EntityDeathEvent event) {
        try {
            for (Player player : this.onlinePlayers()) {
                if (!this.flags.of(player.getUid()).loot) continue;
                if (player.getScene() == null) continue;
                this.vacuum(player, LOOT_RADIUS);
            }
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] onDeath loot failed: {}", t.toString());
        }
    }

    private void onTick(ServerTickEvent event) {
        try {
            this.tickCounter++;

            Remote.syncState(this);
            Remote.drainCommands(this);
            CheatApi.tick(this);

            List<Player> players = this.onlinePlayers();
            boolean needHeal = false;

            for (Player player : players) {
                Flags.PlayerFlags f = this.flags.of(player.getUid());
                if (f.god || f.fall) needHeal = true;
                if (f.loot && this.tickCounter % 2 == 0) {
                    int got = this.vacuum(player, LOOT_RADIUS);
                    if (got > 0 && this.tickCounter % 10 == 0)
                        this.getLogger().info("[CheatMenu] auto-loot: {} item(s), scanned {}",
                                got, this.lastLootScanned);
                }
                if (this.aura && this.tickCounter % 2 == 0) this.auraTick(player);
            }

            if (needHeal && System.currentTimeMillis() - this.lastHealMs > 500L) {
                this.lastHealMs = System.currentTimeMillis();
                for (Player player : players) {
                    Flags.PlayerFlags f = this.flags.of(player.getUid());
                    if (f.god || f.fall) this.topUpHp(player);
                }
            }

            if (this.esp && this.tickCounter % 2 == 0) {
                Bridge.write(this.getServer());
            }
            if (this.tickCounter % 5 == 0) {
                Remote.writeStatus(this, players.isEmpty() ? null : players.get(0));
            }
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] onTick failed: {}", t.toString());
        }
    }

    private void onJoin(PlayerJoinEvent event) {
        try {
            Player player = event.getPlayer();
            if (player != null) this.flags.of(player.getUid());
        } catch (Throwable ignored) {
        }
    }

    private void onQuit(PlayerQuitEvent event) {
        try {
            Player player = event.getPlayer();
            if (player != null) this.flags.drop(player.getUid());
        } catch (Throwable ignored) {
        }
    }

    // --------------------------------------------------------------- actions

    public void topUpHp(Player player) {
        try {
            EntityAvatar avatar = player.getTeamManager().getCurrentAvatarEntity();
            if (avatar == null) return;
            float max = avatar.getFightProperty(FightProperty.FIGHT_PROP_MAX_HP);
            float cur = avatar.getFightProperty(FightProperty.FIGHT_PROP_CUR_HP);
            if (max <= 0f || cur >= max - 0.5f) return;
            player.setHealth(max);
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] heal failed: {}", t.toString());
        }
    }

    public int vacuum(Player player, double radius) {
        Scene scene = player.getScene();
        if (scene == null || scene.getEntities() == null) return 0;

        Position origin = player.getPosition();
        if (origin == null) return 0;

        List<GameEntity> picked = new ArrayList<>();
        List<GameItem> gained = new ArrayList<>();
        int scanned = 0;
        int moved = 0;

        for (GameEntity entity : new ArrayList<>(scene.getEntities().values())) {
            if (!(entity instanceof EntityItem drop)) continue;
            scanned++;
            Position pos = drop.getPosition();
            if (pos != null && radius > 0 && distance2(origin, pos) > radius * radius) continue;

            GameItem item = drop.getItem();
            if (item == null) continue;
            try {
                int itemId = item.getItemId();
                int count = Math.max(1, item.getCount());
                if (itemId <= 0) continue;
                if (!player.getInventory().addItem(itemId, count)) continue;
                moved += count;
                gained.add(new GameItem(itemId, count));
                picked.add(drop);
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] loot skip {}: {}", item.getItemId(), t.toString());
            }
        }

        for (GameEntity drop : picked) {
            try {
                scene.removeEntity(drop, VisionType.VisionType_VISION_REMOVE);
            } catch (Throwable ignored) {
            }
        }

        // Without this the client never shows the "+item" hint, which is why auto-loot looked dead
        // even when the inventory did change.
        if (!gained.isEmpty()) {
            try {
                player.sendPacket(new PacketItemAddHintNotify(gained, ActionReason.Gather));
            } catch (Throwable ignored) {
            }
        }
        this.lastLootScanned = scanned;
        this.lastLootPicked = picked.size();
        return moved;
    }

    public int lastMobPulled = 0;
    public int lastLootScanned = 0;
    public int lastLootPicked = 0;

    /** KillAura: every tick, hit everything alive around the player. */
    public int auraTick(Player player) {
        EntityAvatar avatar = player.getTeamManager().getCurrentAvatarEntity();
        if (avatar == null) return 0;
        Position origin = player.getPosition();
        if (origin == null) return 0;

        int hits = 0;
        for (EntityMonster monster : monstersNear(player.getScene(), origin, this.auraRadius)) {
            try {
                monster.damage(this.auraDamage, avatar.getId(), ElementType.None);
                hits++;
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] aura hit failed: {}", t.toString());
            }
        }
        return hits;
    }

    public List<EntityMonster> monstersNear(Scene scene, Position origin, double radius) {
        List<EntityMonster> out = new ArrayList<>();
        if (scene == null || scene.getEntities() == null || origin == null) return out;
        for (GameEntity entity : scene.getEntities().values()) {
            if (!(entity instanceof EntityMonster monster)) continue;
            Position pos = monster.getPosition();
            if (pos == null) continue;
            if (distance2(origin, pos) <= radius * radius) out.add(monster);
        }
        return out;
    }

    // ---------------------------------------------------------------- 新增：吸附 / 大招 / 标点
    /** 把范围内的怪物拉到玩家身边，并通知客户端它们移动了。 */
    public int mobVacuum(Player player, double radius) {
        Scene scene = player.getScene();
        if (scene == null || scene.getEntities() == null) return 0;
        Position origin = player.getPosition();
        if (origin == null) return 0;

        int moved = 0;
        double r2 = radius * radius;
        for (GameEntity entity : new ArrayList<>(scene.getEntities().values())) {
            if (!(entity instanceof EntityMonster)) continue;
            Position pos = entity.getPosition();
            if (pos == null) continue;
            double d2 = distance2(origin, pos);
            if (d2 <= 9.0 || d2 > r2) continue;
            try {
                double angle = Math.random() * Math.PI * 2;
                double dist = 1.2 + Math.random() * 1.5;
                Position target = new Position(
                        (float) (origin.getX() + Math.cos(angle) * dist),
                        origin.getY(),
                        (float) (origin.getZ() + Math.sin(angle) * dist));
                pos.set(target);

                // A bare pos-only MotionInfo gets ignored by the 7.1 client: it wants rotation,
                // speed and a valid motion state, plus sceneTime/reliableSeq on the wrapper.
                var rot = Vector.newBuilder().setX(0f).setY(0f).setZ(0f).build();
                var speed = Vector.newBuilder().setX(0f).setY(0f).setZ(0f).build();
                var motion = MotionInfo.newBuilder()
                        .setPos(target.toProto())
                        .setRot(rot)
                        .setSpeed(speed)
                        .setState(MotionState.MotionState_MOTION_STANDBY)
                        .build();
                int sceneTime = player.getWorld() == null ? 0 : player.getWorld().getGameTime();
                var info = EntityMoveInfo.newBuilder()
                        .setEntityId(entity.getId())
                        .setMotionInfo(motion)
                        .setSceneTime(sceneTime)
                        .setIsReliable(false)
                        .setReliableSeq(0)
                        .build();
                player.sendPacket(new PacketSceneEntityMoveNotify(info));
                this.lastMobPulled++;
                moved++;
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] mobvac skip {}: {}", entity.getId(), t.toString());
            }
        }
        return moved;
    }

    /** 取当前场景里离玩家最远的地图标点（就是刚在图上扎的那根针）。 */
    public Position mapPinPosition(Player player) {
        if (player.getMapMarksManager() == null) return null;
        var marks = player.getMapMarksManager().getMapMarks();
        if (marks == null || marks.isEmpty()) return null;

        Position origin = player.getPosition();
        Position best = null;
        double bestD = -1;
        for (var mark : marks.values()) {
            if (mark.getSceneId() != player.getSceneId()) continue;
            Position p = mark.getPosition();
            if (p == null) continue;
            double d = origin == null ? 0 : distance2(origin, p);
            if (d > bestD) {
                bestD = d;
                best = p;
            }
        }
        return best;
    }

    /** 把出战队伍的元素能量拉满，客户端就能一直放大招。 */
    public int refillBurst(Player player) {
        int n = 0;
        for (EntityAvatar entity : player.getTeamManager().getActiveTeam()) {
            try {
                if (entity == null || entity.getAvatar() == null) continue;
                var avatar = entity.getAvatar();
                avatar.setCurrentEnergy(99999f);   // 内部会按角色上限裁剪
                player.sendPacket(new PacketAvatarFightPropUpdateNotify(avatar, avatar.getFightProperties()));
                n++;
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] burst skip: {}", t.toString());
            }
        }
        return n;
    }

    /** 告诉客户端"技能可以用"，用来顶掉战技冷却的表现。 */
    public void pingSkillReady(Player player) {
        player.sendPacket(new PacketCanUseSkillNotify(true));
    }
    public int killAll(Player player) {
        Scene scene = player.getScene();
        if (scene == null) return 0;
        EntityAvatar avatar = player.getTeamManager().getCurrentAvatarEntity();
        int killerId = avatar == null ? 0 : avatar.getId();

        List<GameEntity> monsters = new ArrayList<>(scene.getEntities().values());
        int killed = 0;
        for (GameEntity entity : monsters) {
            if (!(entity instanceof EntityMonster)) continue;
            try {
                scene.killEntity(entity, killerId);
                killed++;
            } catch (Throwable ignored) {
            }
        }
        return killed;
    }

    /** Nearest chest-ish gadget; chest gadget ids live in the 702xxxxx block. */
    public EntityGadget nearestChest(Player player) {
        Scene scene = player.getScene();
        if (scene == null) return null;
        Position origin = player.getPosition();
        if (origin == null) return null;

        return scene.getEntities().values().stream()
                .filter(e -> e instanceof EntityGadget)
                .map(e -> (EntityGadget) e)
                .filter(g -> {
                    int id = g.getGadgetId();
                    return id >= 70200000 && id < 70300000;
                })
                .min(Comparator.comparingDouble(g -> distance2(origin, g.getPosition())))
                .orElse(null);
    }

    public List<Player> onlinePlayers() {
        List<Player> out = new ArrayList<>();
        try {
            for (Player player : this.getServer().getPlayers().values()) {
                if (player != null && player.getScene() != null) out.add(player);
            }
        } catch (Throwable ignored) {
        }
        return out;
    }

    public static double distance2(Position a, Position b) {
        if (a == null || b == null) return Double.MAX_VALUE;
        double dx = a.getX() - b.getX();
        double dy = a.getY() - b.getY();
        double dz = a.getZ() - b.getZ();
        return dx * dx + dy * dy + dz * dz;
    }
}
