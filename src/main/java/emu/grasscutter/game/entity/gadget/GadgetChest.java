package emu.grasscutter.game.entity.gadget;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.data.common.ItemParamData;
import emu.grasscutter.data.excels.RewardPreviewData;
import emu.grasscutter.game.drop.BossChestDropTagResolver;
import emu.grasscutter.game.drop.DropSystem;
import emu.grasscutter.game.entity.EntityBaseGadget;
import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.entity.gadget.chest.BossChestInteractHandler;
import emu.grasscutter.game.entity.gadget.chest.ChestInteractHandler;
import emu.grasscutter.game.entity.gadget.chest.WorldChestLootHelper;
import emu.grasscutter.game.inventory.GameItem;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.props.ActionReason;
import emu.grasscutter.game.props.SceneType;
import emu.grasscutter.game.props.WatcherTriggerType;
import emu.grasscutter.game.world.NodKraiExploreSpawnHelper;
import emu.grasscutter.game.world.WorldBossSpawnHelper;
import emu.grasscutter.net.packet.BasePacket;
import emu.grasscutter.net.proto.BossChestInfoOuterClass;
import emu.grasscutter.net.proto.GadgetInteractReqOuterClass;
import emu.grasscutter.net.proto.InterOpTypeOuterClass;
import emu.grasscutter.net.proto.InteractTypeOuterClass;
import emu.grasscutter.net.proto.ResinCostTypeOuterClass;
import emu.grasscutter.net.proto.SceneGadgetInfoOuterClass;
import emu.grasscutter.net.proto.VisionTypeOuterClass;
import emu.grasscutter.scripts.SceneScriptManager;
import emu.grasscutter.scripts.data.SceneBossChest;
import emu.grasscutter.scripts.data.SceneGadget;
import emu.grasscutter.scripts.data.SceneGroup;
import emu.grasscutter.scripts.data.SceneMonster;
import emu.grasscutter.server.packet.send.PacketGadgetAutoPickDropInfoNotify;
import emu.grasscutter.server.packet.send.PacketGadgetInteractRsp;
import emu.grasscutter.server.packet.send.PacketSceneEntityAppearNotify;
import emu.grasscutter.server.packet.send.PacketWorldChestOpenNotify;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * World chests + world-boss trounce blossoms. When DropTable misses ChestDrop ids,
 * falls back to InvestigationMonster reward preview.
 */
public class GadgetChest extends GadgetContent {
    private static final int CHEST_OPENED_STATE = 102;
    private boolean ordinaryClaimed;
    // Boss flowers: every player near the flower when it first appears claims once.
    private volatile List<Integer> bossUids;
    /**
     * Who counts as having fought for a boss flower. The whole scene is the whole open world, and a
     * player elsewhere on the map who never claims would keep the flower open and the boss from
     * respawning.
     */
    private static final double BOSS_CLAIM_RANGE = 100;
    private final Set<Integer> claimedBossUids = ConcurrentHashMap.newKeySet();

    public GadgetChest(EntityGadget gadget) {
        super(gadget);
    }

    @Override
    public synchronized boolean onInteract(
            Player player, GadgetInteractReqOuterClass.GadgetInteractReq req) {
        if (Grasscutter.getConfig().server.game.enableScriptInBigWorld) {
            SceneGadget meta = resolveChestMeta();
            DropSystem dropSystem = player.getServer().getDropSystem();
            if (meta != null && meta.boss_chest != null) {
                if (finishIfAbandoned(player, meta)) {
                    return false;
                }
                if (!canClaimBossFlower(player)) {
                    return false;
                }
                if (req.getOpType() == InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START) {
                    player.sendPacket(
                            new PacketGadgetInteractRsp(
                                    getGadget(),
                                    InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                                    InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START));
                    return false;
                }
                int resinCost = meta.boss_chest.resin > 0 ? meta.boss_chest.resin : 40;
                boolean spent = player.getResinManager().useResin(resinCost);
                boolean ok = false;
                if (spent) {
                    try {
                        if (meta.drop_tag != null) {
                            ok = dropSystem.handleBossChestDrop(meta.drop_tag, player);
                        }
                    } catch (Throwable t) {
                        player.getResinManager().addResin(resinCost);
                        Grasscutter.getLogger()
                                .warn(
                                        "Boss chest drop threw group={} drop_tag={} uid={}",
                                        getGadget().getGroupId(),
                                        meta.drop_tag,
                                        player.getUid(),
                                        t);
                        return false;
                    }
                    if (!ok) {
                        ok = grantBossChestFallback(player, meta);
                    }
                }
                if (ok) {
                    try {
                        player
                                .getBattlePassManager()
                                .triggerMission(
                                        WatcherTriggerType.TRIGGER_WORLD_BOSS_REWARD,
                                        meta.boss_chest.monster_config_id,
                                        1);
                    } catch (Throwable ignored) {
                    }
                    finishBossClaim(player, meta);
                    Grasscutter.getLogger()
                            .info(
                                    "Trounce flower claimed uid={} group={} tag={}",
                                    player.getUid(),
                                    getGadget().getGroupId(),
                                    meta.drop_tag);
                    return false;
                }
                if (spent) {
                    player.getResinManager().addResin(resinCost);
                }
                Grasscutter.getLogger()
                        .warn(
                                "Trounce flower claim failed uid={} group={} tag={} spent={}",
                                player.getUid(),
                                getGadget().getGroupId(),
                                meta.drop_tag,
                                spent);
                return false;
            }
            if (!canOpenOrdinary(player)) {
                return false;
            }
            if (req.getOpType() == InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START) {
                player.sendPacket(
                        new PacketGadgetInteractRsp(
                                getGadget(),
                                InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                                InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START));
                return false;
            }
            grantOrdinaryChestLoot(player);
            finishOpen(player, meta);
            return false;
        }

        ChestInteractHandler handler =
                getGadget()
                        .getScene()
                        .getWorld()
                        .getServer()
                        .getWorldDataSystem()
                        .getChestInteractHandlerMap()
                        .get(getGadget().getGadgetData().getJsonName());
        if (handler == null) {
            if (!canOpenOrdinary(player)) {
                return false;
            }
            if (req.getOpType() == InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START) {
                player.sendPacket(
                        new PacketGadgetInteractRsp(
                                getGadget(),
                                InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                                InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START));
                return false;
            }
            grantOrdinaryChestLoot(player);
            finishOpen(player, null);
            return false;
        }

        if (!(handler instanceof BossChestInteractHandler) && !canOpenOrdinary(player)) {
            return false;
        }
        if (req.getOpType() == InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START
                && handler.isTwoStep()) {
            player.sendPacket(
                    new PacketGadgetInteractRsp(
                            getGadget(),
                            InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                            InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_START));
            return false;
        }

        boolean success;
        if (handler instanceof BossChestInteractHandler bossHandler) {
            var gadgetMeta = getGadget().getMetaGadget();
            boolean bossMeta = gadgetMeta != null && gadgetMeta.boss_chest != null;
            if (bossMeta && finishIfAbandoned(player, gadgetMeta)) {
                return false;
            }
            if (bossMeta && !canClaimBossFlower(player)) {
                return false;
            }
            success =
                    bossHandler.onInteract(
                            this,
                            player,
                            req.getResinCostType()
                                    == ResinCostTypeOuterClass.ResinCostType.ResinCostType_CONDENSE);
        } else {
            grantOrdinaryChestLoot(player);
            success = true;
        }
        if (!success) {
            return false;
        }
        if (getGadget().getMetaGadget() != null && getGadget().getMetaGadget().boss_chest != null) {
            finishBossClaim(player, getGadget().getMetaGadget());
        } else {
            finishOpen(player, null);
        }
        return false;
    }

    /** Ordinary chests open once, for the world host. */
    private boolean canOpenOrdinary(Player player) {
        return player == player.getWorld().getHost()
                && !ordinaryClaimed
                && getGadget().getState() != CHEST_OPENED_STATE;
    }

    private boolean canClaimBossFlower(Player player) {
        List<Integer> uids = bossUids;
        return getGadget().getState() != CHEST_OPENED_STATE
                && uids != null
                && uids.contains(player.getUid())
                && !claimedBossUids.contains(player.getUid());
    }

    /** A player who already claimed re-checks whether the others have left, without new loot. */
    private boolean finishIfAbandoned(Player player, SceneGadget meta) {
        if (getGadget().getState() == CHEST_OPENED_STATE
                || bossUids == null
                || !bossUids.contains(player.getUid())
                || !claimedBossUids.contains(player.getUid())) {
            return false;
        }
        if (!finishIfAllClaimed(player, meta, false)) {
            // Others still have to claim; still answer, or the client's interaction never completes.
            player.sendPacket(
                    new PacketGadgetInteractRsp(
                            getGadget(),
                            InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                            InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_FINISH));
        }
        return true;
    }

    private void finishBossClaim(Player player, SceneGadget meta) {
        claimedBossUids.add(player.getUid());
        if (finishIfAllClaimed(player, meta, true)) {
            return;
        }
        player.sendPacket(
                new PacketGadgetInteractRsp(
                        getGadget(),
                        InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                        InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_FINISH));
        // Refresh the flower for the claimer: its remaining-claimant list no longer has them, and a
        // stale copy keeps offering a claim that would be refused.
        player.sendPacket(
                new PacketSceneEntityAppearNotify(
                        getGadget(), VisionTypeOuterClass.VisionType.VisionType_VISION_REFRESH, 0));
        creditChestOpen(player);
    }

    /** Opens the flower for good once every eligible player still in the scene has claimed. */
    private boolean finishIfAllClaimed(Player player, SceneGadget meta, boolean credit) {
        boolean allClaimed =
                getGadget().getScene().getPlayers().stream()
                        .map(Player::getUid)
                        .filter(bossUids::contains)
                        .allMatch(claimedBossUids::contains);
        if (allClaimed) {
            getGadget().getScene().getBlossomManager().retireChest(getGadget());
            finishOpen(player, meta, credit);
            WorldBossSpawnHelper.onBossChestClaimed(getGadget());
        }
        return allClaimed;
    }

    private void grantOrdinaryChestLoot(Player player) {
        ordinaryClaimed = true;
        EntityGadget gadget = getGadget();
        SceneGadget meta = resolveChestMeta();
        if (meta != null && meta.chest_drop_id > 0) {
            DropSystem dropSystem = player.getServer().getDropSystem();
            // Quest-created chests have no Lua group; scripted quest chests are identified by loot.
            if ((gadget.getScene().getSceneType() == SceneType.SCENE_DUNGEON
                            || gadget.getGroupId() == 0
                            || dropSystem.hasQuestItemDrop(meta.chest_drop_id))
                    && dropSystem.handleChestDrop(
                            meta.chest_drop_id, Math.max(1, meta.drop_count), gadget)) {
                return;
            }
        }
        WorldChestLootHelper.grant(player, gadget);
    }

    protected boolean grantBossChestFallback(Player player, SceneGadget meta) {
        return emu.grasscutter.game.drop.WorldBossChestLootHelper.grant(
                player, meta, getGadget().getGroupId());
    }

    private void finishOpen(Player player, SceneGadget sceneGadget) {
        finishOpen(player, sceneGadget, true);
    }

    private void finishOpen(Player player, SceneGadget sceneGadget, boolean credit) {
        EntityGadget entityGadget = getGadget();
        entityGadget.updateState(CHEST_OPENED_STATE);
        player.sendPacket(
                new PacketGadgetInteractRsp(
                        (EntityBaseGadget) entityGadget,
                        InteractTypeOuterClass.InteractType.InteractType_INTERACT_OPEN_CHEST,
                        InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_FINISH));
        int configId = sceneGadget != null ? sceneGadget.config_id : entityGadget.getConfigId();
        try {
            player.sendPacket(
                    new PacketWorldChestOpenNotify(
                            entityGadget.getGroupId(), player.getSceneId(), configId));
        } catch (Throwable ignored) {
        }
        scheduleDespawn(entityGadget);
        try {
            NodKraiExploreSpawnHelper.markClaimed(getGadget());
        } catch (Throwable ignored) {
        }
        try {
            emu.grasscutter.game.world.SnezhnayaExploreSpawnHelper.markClaimed(getGadget());
        } catch (Throwable ignored) {
        }
        if (credit) {
            creditChestOpen(player);
        }
    }

    private void creditChestOpen(Player player) {
        try {
            int gadgetId = getGadget().getGadgetId();
            player.getPlayerProgress().addHandbookChestOpen(1);
            emu.grasscutter.game.player.InvestigationHandbookHelper.trigger(
                    player,
                    emu.grasscutter.game.props.WatcherTriggerType.TRIGGER_OPEN_CHEST_WITH_GADGET_ID,
                    gadgetId,
                    1);
        } catch (Throwable ignored) {
        }
    }

    private void scheduleDespawn(EntityGadget entityGadget) {
        try {
            if (entityGadget == null || entityGadget.getScene() == null) {
                return;
            }
            // Boss trounce flowers: remove immediately with VISION_REMOVE so they do not linger
            // as opened/smoke visuals while the world boss already respawned.
            boolean bossFlower =
                    entityGadget.getMetaGadget() != null
                            && entityGadget.getMetaGadget().boss_chest != null;
            // Scene scheduler ticks once per scene tick (~1s); 2 ≈ brief open animation then vanish.
            int delayTicks = bossFlower ? 0 : 2;
            int entityId = entityGadget.getId();
            Runnable despawn =
                    () -> {
                        try {
                            GameEntity entity = entityGadget.getScene().getEntityById(entityId);
                            if (entity instanceof EntityGadget) {
                                entityGadget
                                        .getScene()
                                        .removeEntity(
                                                entity,
                                                VisionTypeOuterClass.VisionType.VisionType_VISION_REMOVE);
                            }
                        } catch (Throwable t) {
                            Grasscutter.getLogger().debug("Chest delayed despawn: {}", t.toString());
                        }
                    };
            if (delayTicks <= 0) {
                despawn.run();
            } else {
                entityGadget.getScene().getScheduler().scheduleDelayedTask(despawn, delayTicks);
            }
        } catch (Throwable t) {
            Grasscutter.getLogger().warn("Chest scheduleDespawn failed: {}", t.toString());
        }
    }

    @Override
    public void onBuildProto(SceneGadgetInfoOuterClass.SceneGadgetInfo.Builder builder) {
        SceneGadget sceneGadget = resolveChestMeta();
        if (sceneGadget == null) {
            return;
        }
        SceneBossChest bossChest = sceneGadget.boss_chest;
        if (bossChest != null) {
            if (bossUids == null) {
                var flower = getGadget().getPosition();
                List<Integer> present =
                        getGadget().getScene().getPlayers().stream()
                                .filter(p -> flower == null
                                        || (p.getPosition() != null
                                                && p.getPosition().computeDistance(flower) <= BOSS_CLAIM_RANGE))
                                .map(Player::getUid)
                                .toList();
                if (!present.isEmpty()) {
                    bossUids = present;
                }
            }
            List<Integer> uids = bossUids != null ? bossUids : List.of();
            builder.setBossChest(
                    BossChestInfoOuterClass.BossChestInfo.newBuilder()
                            .setMonsterConfigId(bossChest.monster_config_id)
                            .setResin(bossChest.resin)
                            .addAllQualifyUidList(uids)
                            .addAllRemainUidList(
                                    uids.stream().filter(uid -> !claimedBossUids.contains(uid)).toList())
                            .build());
        }
    }

    private SceneGadget resolveChestMeta() {
        EntityGadget entityGadget = getGadget();
        SceneGadget meta = entityGadget.getMetaGadget();
        if (meta != null && meta.boss_chest != null && meta.drop_tag != null) {
            return meta;
        }
        if (entityGadget.getScene() == null) {
            return meta;
        }
        SceneScriptManager scriptManager = entityGadget.getScene().getScriptManager();
        if (scriptManager == null) {
            return meta;
        }
        SceneGroup group = scriptManager.getGroupById(entityGadget.getGroupId());
        if (group == null) {
            return meta;
        }
        if (group.gadgets == null || group.gadgets.isEmpty()) {
            try {
                scriptManager.loadGroupFromScript(group);
            } catch (Throwable ignored) {
            }
            group = scriptManager.getGroupById(entityGadget.getGroupId());
            if (group == null || group.gadgets == null) {
                return meta;
            }
        }
        SceneGadget fromGroup = group.gadgets.get(entityGadget.getConfigId());
        if (fromGroup == null) {
            return meta;
        }
        fromGroup.group = group;
        entityGadget.setMetaGadget(fromGroup);
        return fromGroup;
    }
}
