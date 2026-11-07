package cheatmenu;

import emu.grasscutter.server.packet.send.PacketScenePlayerLocationNotify;

import emu.grasscutter.game.entity.gadget.GadgetChest;
import emu.grasscutter.net.proto.InterOpTypeOuterClass.InterOpType;
import emu.grasscutter.net.proto.InteractTypeOuterClass.InteractType;

import emu.grasscutter.net.proto.AvatarSkillInfoNotifyOuterClass.AvatarSkillInfoNotify;
import emu.grasscutter.net.proto.AvatarSkillInfoOuterClass.AvatarSkillInfo;

import emu.grasscutter.data.GameData;

import emu.grasscutter.game.entity.gadget.GadgetGatherObject;
import emu.grasscutter.net.proto.GadgetInteractReqOuterClass.GadgetInteractReq;

import emu.grasscutter.net.proto.AbilityStringOuterClass.AbilityString;
import emu.grasscutter.net.proto.ModifierPropertyOuterClass.ModifierProperty;
import emu.grasscutter.net.proto.AbilityMetaModifierChangeOuterClass.AbilityMetaModifierChange;
import emu.grasscutter.net.proto.AbilityInvokeEntryHeadOuterClass.AbilityInvokeEntryHead;
import emu.grasscutter.net.proto.AbilityInvokeEntryOuterClass.AbilityInvokeEntry;
import emu.grasscutter.net.proto.AbilityInvokeArgumentOuterClass.AbilityInvokeArgument;
import emu.grasscutter.net.proto.ForwardTypeOuterClass.ForwardType;
import emu.grasscutter.server.packet.send.PacketAbilityInvocationsNotify;

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

    private boolean extraHitBusy = false;

    private void onDamage(EntityDamageEvent event) {
        try {
            GameEntity damager = event.getDamager();

            // Damage the player deals: scale it (Akebi's "Damage x N" / one-hit).
            if (this.mult != 1.0f && damager instanceof EntityAvatar attacker
                    && attacker.getPlayer() != null) {
                event.setDamage(event.getDamage() * this.mult);
            }

            // 多段伤害：一刀打出 N 次伤害（Akebi 的 multi-hit）
            // 用 extraHitBusy 防止补的伤害再次触发本回调形成递归
            if (CheatApi.hits > 1 && !extraHitBusy && damager instanceof EntityAvatar hitter
                    && hitter.getPlayer() != null) {
                GameEntity target = event.getEntity();
                if (target != null && target.isAlive()) {
                    extraHitBusy = true;
                    try {
                        final float base = event.getDamage();
                        final int extra = CheatApi.hits - 1;
                        for (int i = 0; i < extra && target.isAlive(); ++i) {
                            target.damage(base, hitter.getId(), event.getAttackElementType());
                        }
                    } finally {
                        extraHitBusy = false;
                    }
                }
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
        // 地图上的采集物（日落果、树莓这些）是 GadgetGatherObject，不是 EntityItem。
        // 它们有公开的 onInteract，直接调就是正常采集一遍（发物品、播采集表现、处理刷新）。
        int gathered = 0;
        for (GameEntity entity : new ArrayList<>(scene.getEntities().values())) {
            if (!(entity instanceof EntityGadget gadget)) continue;
            if (!(gadget.getContent() instanceof GadgetGatherObject gather)) continue;
            Position pos = gadget.getPosition();
            if (pos == null) continue;
            if (radius > 0 && origin != null && distance2(origin, pos) > radius * radius) continue;
            scanned++;
            try {
                if (gather.onInteract(player, GadgetInteractReq.newBuilder().build())) {
                    scene.removeEntity(gadget, VisionType.VisionType_VISION_REMOVE);
                    gathered++;
                }
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] gather skip {}: {}", gadget.getConfigId(),
                        t.toString());
            }
        }
        moved += gathered;

        this.lastLootScanned = scanned;
        this.lastLootPicked = picked.size() + gathered;
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

    /**
     * 移动速度：给客户端发一个带 Animator_MoveSpeedRatio 属性的 modifier。
     * 属性名来自 resources/BinOutput 的技能数据（0.25 = +25%），客户端自己按比例跑，不碰内存。
     */
    /**
     * 移动速度。
     *
     * <p>先挂上游戏原生的「移速增加药品」buff（BuffExcel 里 serverBuffId=170201，
     * ability=MoveSpeed_lv2_1，默认 +30%），客户端认识这套数据，一定会生效；
     * 然后再用同样的 ability 名把属性覆盖成用户要的倍数。
     */
    public boolean applyMoveSpeed(Player player, float ratio) {
        EntityAvatar avatar = player.getTeamManager().getCurrentAvatarEntity();
        if (avatar == null) return false;
        try {
            // 1) 原生 buff（groupId 21 = 移速增加药品）
            if (!player.getBuffManager().hasBuff(21)) {
                player.getBuffManager().addBuff(170201, 600f, avatar.getAvatar());
            }
            // 第二个原生移速 buff（爬塔用的 MoveSpeed_Dungeon，数据上也是 +30%），
            // 两个 groupId 不同可以叠加，实测能到 +50~60%
            try {
                player.getBuffManager().addBuff(701018, 600f, avatar.getAvatar());
            } catch (Throwable ignored) {
            }
            // 2) 用真实 ability 名覆盖属性值
            var key = AbilityString.newBuilder().setStr("Animator_MoveSpeedRatio").build();
            var prop = ModifierProperty.newBuilder().setKey(key).setValue(ratio).build();
            var change = AbilityMetaModifierChange.newBuilder()
                    .setParentAbilityName(AbilityString.newBuilder().setStr("MoveSpeed_lv2_1"))
                    .setModifierLocalId(0)
                    .setServerBuffUid(170201)
                    .addProperties(prop)
                    .build();
            var head = AbilityInvokeEntryHead.newBuilder().setInstancedAbilityId(0).build();
            var entry = AbilityInvokeEntry.newBuilder()
                    .setEntityId(avatar.getId())
                    .setArgumentType(
                            AbilityInvokeArgument.AbilityInvokeArgument_ABILITY_META_MODIFIER_CHANGE)
                    .setForwardType(ForwardType.ForwardType_FORWARD_TO_ALL)
                    .setHead(head)
                    .setAbilityData(change.toByteString())
                    .build();
            player.sendPacket(new PacketAbilityInvocationsNotify(entry));
            this.lastSpeedSent = ratio;
            return true;
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] speed failed: {}", t.toString());
            return false;
        }
    }

    /** 关掉加速：把移速药 buff 移除（groupId 21）。 */
    public boolean clearMoveSpeed(Player player) {
        try {
            player.getBuffManager().removeBuff(21);
            this.lastSpeedSent = 0f;
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * 反重力（模拟流浪者 E 的悬空）。
     *
     * <p>从 ConfigAbility_Avatar_Wanderer.json 挖出来的原始数据：
     *
     * <pre>
     * ability  Avatar_Wanderer_ElementalArt_HoverAir
     * modifier Initialize
     *   properties { Actor_AddGravityScale: -2.0 }
     * </pre>
     *
     * <p>也就是说"飞行"本质上是给角色挂一个负重力修饰器。这里直接把同一个属性发给当前角色，
     * 看客户端认不认（能力配置是全局的，所有客户端都有这份文件，所以有希望）。
     */
    public boolean applyGravity(Player player, float scale) {
        try {
            EntityAvatar avatar = player.getTeamManager().getCurrentAvatarEntity();
            if (avatar == null) return false;

            var key = AbilityString.newBuilder().setStr("Actor_AddGravityScale").build();
            var prop = ModifierProperty.newBuilder().setKey(key).setValue(scale).build();
            var change =
                    AbilityMetaModifierChange.newBuilder()
                            .setParentAbilityName(
                                    AbilityString.newBuilder()
                                            .setStr("Avatar_Wanderer_ElementalArt_HoverAir"))
                            .setModifierLocalId(0)
                            .addProperties(prop)
                            .build();
            var head = AbilityInvokeEntryHead.newBuilder().setInstancedAbilityId(0).build();
            var entry =
                    AbilityInvokeEntry.newBuilder()
                            .setEntityId(avatar.getId())
                            .setArgumentType(
                                    AbilityInvokeArgument
                                            .AbilityInvokeArgument_ABILITY_META_MODIFIER_CHANGE)
                            .setForwardType(ForwardType.ForwardType_FORWARD_TO_ALL)
                            .setHead(head)
                            .setAbilityData(change.toByteString())
                            .build();
            player.sendPacket(new PacketAbilityInvocationsNotify(entry));
            this.lastGravitySent = scale;
            return true;
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] gravity failed: {}", t.toString());
            return false;
        }
    }

    public float lastGravitySent = 0f;

    public float lastSpeedSent = 0f;

    /**
     * 传送飞行：按当前朝向把角色往前挪一段。
     *
     * <p>服务端的 teleport 是权威操作，客户端会直接跟随，**不经过碰撞检测**，
     * 所以连续小步传送就等于穿墙飞行。这是"灵魂出窍/自由移动"在服务端这一侧能做到的替代品。
     */
    /**
     * 飞行一步。
     *
     * <p>客户端对自己的角色是权威的，服务端发 EntityMove 会被无视（实测），所以这里还是得用
     * teleport。但 teleport 内部是 transferPlayerToScene，**高频调用会让客户端进加载页卡死**，
     * 因此调用方必须控制频率（800ms 一次、一步 6~8 米），做成"跳跃式"飞行。
     */
    /** 列出玩家在当前场景已解锁的传送点（含坐标），"第一开锚点"就是列表里第一个。 */
    public java.util.List<int[]> anchorList(Player player) {
        java.util.List<int[]> out = new java.util.ArrayList<>();
        if (player.getScene() == null) return out;
        int sceneId = player.getSceneId();
        var points = player.getUnlockedScenePoints(sceneId);
        if (points == null) return out;
        for (int id : points) {
            out.add(new int[] {id, sceneId});
        }
        return out;
    }

    public Position anchorPos(int sceneId, int pointId) {
        try {
            var entry = GameData.getScenePointEntryById(sceneId, pointId);
            if (entry == null) return null;
            var pd = entry.getPointData();
            if (pd == null) return null;
            return pd.getTranPos() != null ? pd.getTranPos() : pd.getPos();
        } catch (Throwable t) {
            return null;
        }
    }

    public boolean flyStep(Player player, float meters) {
        try {
            Position pos = player.getPosition();
            if (pos == null) return false;
            double yaw =
                    Math.toRadians(player.getRotation() == null ? 0 : player.getRotation().getY());
            float fx = (float) (-Math.sin(yaw)) * meters;
            float fz = (float) (-Math.cos(yaw)) * meters;
            Position target = new Position(pos.getX() + fx, pos.getY(), pos.getZ() + fz);
            player.teleport(target);
            return true;
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] fly failed: {}", t.toString());
            return false;
        }
    }

    /** 垂直升降一步（不用朝向，直接改 Y）。 */
    public boolean flyLift(Player player, float meters) {
        try {
            Position pos = player.getPosition();
            if (pos == null) return false;
            player.teleport(new Position(pos.getX(), pos.getY() + meters, pos.getZ()));
            return true;
        } catch (Throwable t) {
            return false;
        }
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
    /**
     * 任务地点：客户端把"追踪中的任务"作为地图标点同步给服务端，MapMark 里带 questId。
     * 找当前场景里 questId > 0 的那个标点，就是任务要你去的位置。
     */
    /**
     * 自动交互：把范围内所有可交互的宝箱/机关直接打开。
     *
     * <p>走的是 GadgetContent.onInteract —— 和玩家自己点 F 是同一条服务端路径，
     * 所以奖励、任务进度、开启记录都会正常结算。
     */
    /** 每个玩家上一次的坐标，用来判断"他正在往哪走"。 */
    private final java.util.Map<Integer, Position> lastPos = new java.util.concurrent.ConcurrentHashMap<>();
    private final java.util.Map<Integer, Long> lastInteractAt = new java.util.concurrent.ConcurrentHashMap<>();

    /**
     * 穿墙（noclip）：teleport 不检查碰撞，所以沿玩家移动方向做小步瞬移就能穿墙/穿山/穿地板。
     *
     * @return 实际移动了多少米（0 表示这一帧玩家没动）
     */
    /**
     * 平滑移动：只改服务端记录的坐标并广播位置包，不触发 transferPlayerToScene（不白屏）。
     *
     * <p>官方也在用 ScenePlayerLocationNotify 纠正玩家位置（源码注释 "official does send this"），
     * 所以客户端有机会接受这种"软"位移。
     */
    public boolean smoothMove(Player player, float x, float y, float z) {
        try {
            Position pos = player.getPosition();
            if (pos == null) return false;
            pos.setX(x);
            pos.setY(y);
            pos.setZ(z);
            var scene = player.getScene();
            if (scene != null) {
                scene.broadcastPacket(new PacketScenePlayerLocationNotify(scene));
            } else {
                player.sendPacket(new PacketScenePlayerLocationNotify(player.getScene()));
            }
            return true;
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] smoothMove failed: {}", t.toString());
            return false;
        }
    }

    public float noclipStep(Player player, float factor) {
        Position now = player.getPosition();
        if (now == null) return 0f;
        Position prev = this.lastPos.put(player.getUid(), new Position(now));
        if (prev == null) return 0f;

        final float dx = now.getX() - prev.getX();
        final float dy = now.getY() - prev.getY();
        final float dz = now.getZ() - prev.getZ();
        final float dist = (float) Math.sqrt(dx * dx + dy * dy + dz * dz);
        // 正常走路一帧位移很小；超过 30 米说明是传送，不要放大
        if (dist < 0.02f || dist > 30.f) return 0f;

        final float extra = dist * (factor - 1f);
        if (extra <= 0.01f) return 0f;
        final float nx = now.getX() + dx / dist * extra;
        final float ny = now.getY() + dy / dist * extra + 0.5f;   // 抬高一点，避免卡进地面
        final float nz = now.getZ() + dz / dist * extra;
        player.teleport(new Position(nx, ny, nz));
        return extra;
    }

    public void forgetPos(int uid) { this.lastPos.remove(uid); }

    /**
     * 宝箱传送：把玩家传到当前场景里最近的宝箱旁边（Akebi 的"宝箱传送"）。
     * 只找 GadgetChest，所以不会误传到机关/神像上。
     *
     * @return 找到了就 teleport 并返回宝箱坐标，否则返回 null
     */
    public Position chestTeleport(Player player) {
        Scene scene = player.getScene();
        if (scene == null) return null;
        Position origin = player.getPosition();
        if (origin == null) return null;

        Position best = null;
        double bestD = Double.MAX_VALUE;
        for (GameEntity entity : scene.getEntities().values()) {
            if (!(entity instanceof EntityGadget gadget)) continue;
            var content = gadget.getContent();
            if (!(content instanceof GadgetChest)) continue;
            Position p = gadget.getPosition();
            if (p == null) continue;
            double d = distance2(origin, p);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
        if (best == null) return null;
        Position safe = new Position(best.getX(), best.getY() + 1.5f, best.getZ());
        player.teleport(safe);
        return safe;
    }

    public int interactAll(Player player, float radius) {
        Scene scene = player.getScene();
        if (scene == null) return 0;
        Position origin = player.getPosition();
        if (origin == null) return 0;
        final float r2 = radius * radius;
        int done = 0;
        for (GameEntity entity : scene.getEntities().values()) {
            if (!(entity instanceof EntityGadget gadget)) continue;
            var content = gadget.getContent();
            if (content == null) continue;
            // 只碰宝箱类，其它机关（神像、工作台、献祭）逻辑各不相同，乱调会出问题
            if (!(content instanceof GadgetChest)) continue;
            Position p = gadget.getPosition();
            if (p == null) continue;
            if (distance2(origin, p) > r2) continue;
            try {
                var req =
                        GadgetInteractReq.newBuilder()
                                .setGadgetEntityId(gadget.getId())
                                .setOpType(InterOpType.InterOpType_INTER_OP_FINISH)
                                .build();
                if (content.onInteract(player, req)) done++;
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] chest {} interact failed: {}",
                        gadget.getId(), t.toString());
            }
        }
        return done;
    }

    public int interactBroad(Player player, float radius, boolean includeAll) {
        Scene scene = player.getScene();
        if (scene == null) return 0;
        Position origin = player.getPosition();
        if (origin == null) return 0;
        final double r2 = (double) radius * radius;
        int done = 0, tried = 0;
        for (GameEntity entity : scene.getEntities().values()) {
            if (!(entity instanceof EntityGadget gadget)) continue;
            var content = gadget.getContent();
            if (content == null) continue;
            boolean chest = content instanceof GadgetChest;
            boolean gather = content instanceof GadgetGatherObject;
            if (!chest && !gather && !includeAll) continue;
            Position p = gadget.getPosition();
            if (p == null) continue;
            if (distance2(origin, p) > r2) continue;
            // 冷却：同一个 gadget 3 秒内不重复触发（否则会刷屏/刷物品）
            final int gid = gadget.getId();
            final long nowMs = System.currentTimeMillis();
            Long lastAt = lastInteractAt.get(gid);
            if (lastAt != null && nowMs - lastAt < 3000L) continue;
            lastInteractAt.put(gid, nowMs);
            if (lastInteractAt.size() > 4096) lastInteractAt.clear();
            tried++;
            try {
                var req = GadgetInteractReq.newBuilder()
                        .setGadgetEntityId(gadget.getId())
                        .setOpType(InterOpType.InterOpType_INTER_OP_FINISH)
                        .build();
                if (content.onInteract(player, req)) {
                    done++;
                    this.getLogger().debug("[CheatMenu] interact ok gadget={} content={}",
                            gadget.getConfigId(), content.getClass().getSimpleName());
                }
            } catch (Throwable t) {
                this.getLogger().debug("[CheatMenu] interact fail gadget={}: {}",
                        gadget.getConfigId(), t.toString());
            }
        }
        if (includeAll) {
            this.getLogger().debug("[CheatMenu] broad interact tried={} done={}", tried, done);
        }
        return done;
    }

    public String questTarget(Player player, Position[] outPos) {
        if (player.getMapMarksManager() == null) return null;
        var marks = player.getMapMarksManager().getMapMarks();
        if (marks == null || marks.isEmpty()) return null;

        Position origin = player.getPosition();
        Position best = null;
        int bestQuest = 0;
        String bestName = null;
        double bestD = Double.MAX_VALUE;
        for (var mark : marks.values()) {
            if (mark.getQuestId() <= 0) continue;
            if (mark.getSceneId() != player.getSceneId()) continue;
            Position p = mark.getPosition();
            if (p == null) continue;
            double d = origin == null ? 0 : distance2(origin, p);
            if (d < bestD) {
                bestD = d;
                best = p;
                bestQuest = mark.getQuestId();
                bestName = mark.getName();
            }
        }
        if (best == null) return null;
        if (outPos != null && outPos.length > 0) outPos[0] = best;
        return "quest " + bestQuest + (bestName != null && !bestName.isEmpty() ? " (" + bestName + ")" : "");
    }

    public Position mapPinPosition(Player player) {
        if (player.getMapMarksManager() == null) return null;
        var marks = player.getMapMarksManager().getMapMarks();
        if (marks == null || marks.isEmpty()) return null;

        Position origin = player.getPosition();
        Position best = null;
        double bestD = -1;
        for (var mark : marks.values()) {
            if (mark.getMapMarkPointType() != emu.grasscutter.net.proto.MapMarkPointTypeOuterClass.MapMarkPointType.MapMarkPointType_SPECIAL) continue;
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
    /**
     * 无限技能：直接把每个技能的冷却时间清零并通知客户端。
     *
     * <p>CD 是客户端本地计时的，光发 "CanUseSkill" 不会重置。服务端有 AvatarSkillInfoNotify，
     * 里面带 passCdTime / fullCdTimeList —— 发 0 就等于告诉客户端"这个技能已经好了"。
     */
    public int resetSkillCd(Player player) {
        EntityAvatar entity = player.getTeamManager().getCurrentAvatarEntity();
        if (entity == null) return 0;
        var avatar = entity.getAvatar();
        if (avatar == null) return 0;
        var depot = avatar.getSkillDepot();
        if (depot == null) return 0;

        var notify = AvatarSkillInfoNotify.newBuilder().setGuid(avatar.getGuid());
        int count = 0;
        try {
            for (int skillId : depot.getSkillsAndEnergySkill().toArray()) {
                if (skillId <= 0) continue;
                notify.putSkillMap(
                        skillId,
                        AvatarSkillInfo.newBuilder()
                                .setPassCdTime(0)
                                .addFullCdTimeList(0)
                                .setMaxChargeCount(1)
                                .build());
                count++;
            }
            BasePacket packet = new BasePacket(PacketOpcodes.AvatarSkillInfoNotify);
            packet.setData(notify.build());
            player.sendPacket(packet);
        } catch (Throwable t) {
            this.getLogger().debug("[CheatMenu] resetCd failed: {}", t.toString());
        }
        return count;
    }

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

    public Position nearestGadgetByTag(Player player, String tag) {
        Scene scene = player.getScene();
        if (scene == null || tag == null) return null;
        Position origin = player.getPosition();
        if (origin == null) return null;
        final String want = tag.toUpperCase(java.util.Locale.ROOT);
        EntityGadget best = null;
        double bestD = Double.MAX_VALUE;
        for (GameEntity e : scene.getEntities().values()) {
            if (!(e instanceof EntityGadget g)) continue;
            Position p = g.getPosition();
            if (p == null) continue;
            boolean match = false;
            try {
                var gdata = GameData.getGadgetDataMap().get(g.getGadgetId());
                if (gdata != null && gdata.getTags() != null) {
                    for (String t : gdata.getTags()) {
                        if (t != null && t.toUpperCase(java.util.Locale.ROOT).contains(want)) {
                            match = true;
                            break;
                        }
                    }
                }
            } catch (Throwable ignored) {
            }
            if (!match) continue;
            double d = distance2(origin, p);
            if (d < bestD) {
                bestD = d;
                best = g;
            }
        }
        return best == null ? null : best.getPosition();
    }

    public Position nearestGadgetByIdRange(Player player, int idMin, int idMax) {
        Scene scene = player.getScene();
        if (scene == null || idMax <= idMin) return null;
        Position origin = player.getPosition();
        if (origin == null) return null;
        GameEntity best = null;
        double bestD = Double.MAX_VALUE;
        for (GameEntity e : scene.getEntities().values()) {
            int id = e.getEntityTypeId();
            if (id < idMin || id >= idMax) continue;
            Position p = e.getPosition();
            if (p == null) continue;
            double d = distance2(origin, p);
            if (d < bestD) {
                bestD = d;
                best = e;
            }
        }
        return best == null ? null : best.getPosition();
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