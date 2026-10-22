package emu.grasscutter.game.managers.blossom;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.database.RewardGrantJournal;
import emu.grasscutter.game.reward.RewardClaimFlow;
import emu.grasscutter.config.Configuration;
import emu.grasscutter.data.*;
import emu.grasscutter.game.inventory.InventoryAddPolicy;
import emu.grasscutter.game.props.ActionReason;
import emu.grasscutter.game.reward.RewardConfiguration;
import emu.grasscutter.game.entity.*;
import emu.grasscutter.game.entity.gadget.GadgetWorktop;
import emu.grasscutter.game.inventory.GameItem;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.world.*;
import emu.grasscutter.net.proto.*;
import emu.grasscutter.server.packet.send.PacketBlossomBriefInfoNotify;
import emu.grasscutter.utils.Utils;
import it.unimi.dsi.fastutil.ints.*;
import java.util.*;

public class BlossomManager {
    private static final Set<BlossomType> MISSING_TYPES_REPORTED =
            java.util.concurrent.ConcurrentHashMap.newKeySet();

    private final Scene scene;
    private final List<BlossomActivity> blossomActivities = new ArrayList<>();
    private final List<BlossomActivity> activeChests = new ArrayList<>();
    private final List<EntityGadget> createdEntity = new ArrayList<>();

    private final List<SpawnDataEntry> blossomConsumed = new ArrayList<>();
    private List<SpawnDataEntry> scriptedSpawnCandidates;
    private final Set<SpawnDataEntry> scriptedSpawned = new HashSet<>();
    private static final double SCRIPTED_SPAWN_RANGE = 400;

    /** The regular spawn grid is bypassed while Lua scene groups are active.
     * Populate the legacy ley-line outcrops from their authored spawn records in both modes.
     */
    public void ensureScriptedSpawns() {
        if (scene.getDungeonManager() != null || scene.getPlayers().isEmpty()) return;
        if (scriptedSpawnCandidates == null) {
            scriptedSpawnCandidates = scriptedCandidates(GameDepot.getSpawnLists(), scene.getId());
            long gold = scriptedSpawnCandidates.stream()
                    .filter(entry -> BlossomType.valueOf(entry.getGadgetId()) == BlossomType.GOLD)
                    .count();
            long blue = scriptedSpawnCandidates.size() - gold;
            Grasscutter.getLogger().info(
                    "Ley-line outcrop sources scene={} wealth={} revelation={}",
                    scene.getId(), gold, blue);
        }
        for (SpawnDataEntry spawn : scriptedSpawnCandidates) {
            if (scriptedSpawned.contains(spawn) || blossomConsumed.contains(spawn)) continue;
            boolean nearby = scene.getPlayers().stream()
                    .anyMatch(player -> player.getPosition().computeDistance(spawn.getPos())
                            <= SCRIPTED_SPAWN_RANGE);
            if (!nearby) continue;

            EntityGadget existing = scene.getEntities().values().stream()
                    .filter(entity -> entity instanceof EntityGadget)
                    .map(entity -> (EntityGadget) entity)
                    .filter(entity -> entity.getGroupId() == spawn.getGroup().getGroupId()
                            && entity.getConfigId() == spawn.getConfigId()
                            && entity.getGadgetId() == spawn.getGadgetId())
                    .findFirst().orElse(null);
            if (existing != null) {
                existing.setSpawnEntry(spawn);
                initBlossom(existing);
                scene.broadcastPacket(new emu.grasscutter.server.packet.send.PacketSceneEntityAppearNotify(
                        existing, VisionTypeOuterClass.VisionType.VisionType_VISION_REFRESH, 0));
            } else {
                var gadget = new EntityGadget(
                        scene, spawn.getGadgetId(), spawn.getPos(), spawn.getRot());
                gadget.setGroupId(spawn.getGroup().getGroupId());
                gadget.setConfigId(spawn.getConfigId());
                gadget.setSpawnEntry(spawn);
                if (spawn.getGadgetState() > 0) gadget.setState(spawn.getGadgetState());
                initBlossom(gadget);
                scene.addEntity(gadget);
            }
            scriptedSpawned.add(spawn);
        }
    }

    /** Use existing spawn coordinates and gadget IDs, never synthetic ley-line positions. */
    static List<SpawnDataEntry> scriptedCandidates(
            Map<SpawnDataEntry.GridBlockId, ? extends List<SpawnDataEntry>> spawnLists,
            int sceneId) {
        return spawnLists.entrySet().stream()
                .filter(entry -> entry.getKey().getSceneId() == sceneId)
                .flatMap(entry -> entry.getValue().stream())
                .filter(entry -> BlossomType.valueOf(entry.getGadgetId()) != null)
                .distinct()
                .toList();
    }

    public BlossomManager(Scene scene) {
        this.scene = scene;
    }

    public void onTick() {
        synchronized (blossomActivities) {
            var it = blossomActivities.iterator();
            while (it.hasNext()) {
                var active = it.next();
                active.onTick();
                if (active.getPass()) {
                    EntityGadget chest = active.getChest();
                    scene.addEntity(chest);
                    scene.setChallenge(null);
                    activeChests.add(active);
                    it.remove();
                }
            }
        }
    }

    public void recycleGadgetEntity(List<GameEntity> entities) {
        for (var entity : entities) {
            if (entity instanceof EntityGadget gadget) {
                createdEntity.remove(gadget);
            }
        }
        notifyIcon();
    }

    public void initBlossom(EntityGadget gadget) {
        if (createdEntity.contains(gadget)) {
            return;
        }
        if (blossomConsumed.contains(gadget.getSpawnEntry())) {
            return;
        }
        var id = gadget.getGadgetId();
        if (BlossomType.valueOf(id) == null) {
            return;
        }
        // Source gadget rows may carry a generic type under 7.1; a ley-line outcrop
        // always needs a worktop to start its combat challenge.
        GadgetWorktop gadgetWorktop = gadget.getContent() instanceof GadgetWorktop worktop
                ? worktop : new GadgetWorktop(gadget);
        gadget.replaceContent(gadgetWorktop);
        gadget.setState(204);
        int worldLevel = getWorldLevel();
        gadgetWorktop.addWorktopOptions(new int[] {187});
        gadgetWorktop.setOnSelectWorktopOptionEvent(
                (GadgetWorktop context, int option) -> {
                    BlossomActivity activity;
                    EntityGadget entityGadget = context.getGadget();
                    synchronized (blossomActivities) {
                        for (BlossomActivity i : this.blossomActivities) {
                            if (i.getGadget() == entityGadget) {
                                return false;
                            }
                        }

                        int volume = 0;
                        IntList monsters = new IntArrayList();
                        while (true) {
                            var remain = GameDepot.getBlossomConfig().getMonsterFightingVolume() - volume;
                            if (remain <= 0) {
                                break;
                            }
                            var rand = Utils.randomRange(1, 100);
                            if (rand > 85 && remain >= 50) { // 15% ,generate strong monster
                                monsters.addAll(getRandomMonstersID(2, 1));
                                volume += 50;
                            } else if (rand > 50 && remain >= 20) { // 35% ,generate normal monster
                                monsters.addAll(getRandomMonstersID(1, 1));
                                volume += 20;
                            } else { // 50% ,generate weak monster
                                monsters.addAll(getRandomMonstersID(0, 1));
                                volume += 10;
                            }
                        }

                        Grasscutter.getLogger().info("Blossom Monsters:" + monsters);

                        activity = new BlossomActivity(entityGadget, monsters, -1, worldLevel);
                        blossomActivities.add(activity);
                    }
                    entityGadget.updateState(201);
                    scene.setChallenge(activity.getChallenge());
                    scene.removeEntity(entityGadget, VisionTypeOuterClass.VisionType.VisionType_VISION_REMOVE);
                    activity.start();
                    return true;
                });
        createdEntity.add(gadget);
        notifyIcon();
    }

    public void notifyIcon() {
        scene.broadcastPacket(new PacketBlossomBriefInfoNotify(buildBriefInfos()));
    }

    /** Sync map markers after EnterSceneDone, including when no outcrop is nearby. */
    public void notifyIcon(Player player) {
        player.sendPacket(new PacketBlossomBriefInfoNotify(buildBriefInfos()));
    }

    private List<BlossomBriefInfoOuterClass.BlossomBriefInfo> buildBriefInfos() {
        final int wl = getWorldLevel();
        final int worldLevel = (wl < 0) ? 0 : ((wl > 9) ? 9 : wl);
        final var worldLevelData = GameData.getWorldLevelDataMap().get(worldLevel);
        final int monsterLevel = (worldLevelData != null) ? worldLevelData.getMonsterLevel() : 1;
        List<BlossomBriefInfoOuterClass.BlossomBriefInfo> blossoms = new ArrayList<>();
        GameDepot.getSpawnLists()
                .forEach(
                        (gridBlockId, spawnDataEntryList) -> {
                            int sceneId = gridBlockId.getSceneId();
                            if (sceneId != scene.getId()) return;
                            // The block list already holds every spawn bucketed into this block, so
                            // walking out to each entry's whole group re-emitted the group once per
                            // entry - and re-emitted spawns that belong to other blocks, which then
                            // got listed again when those blocks came round.
                            spawnDataEntryList.stream()
                                    .filter(spawn -> !blossomConsumed.contains(spawn))
                                    .filter(spawn -> BlossomType.valueOf(spawn.getGadgetId()) != null)
                                    .forEach(
                                            spawn -> {
                                                var type = BlossomType.valueOf(spawn.getGadgetId());
                                                Integer previewReward = getPreviewReward(type, worldLevel);
                                                if (previewReward == null) {
                                                    // No drop table for this blossom at this world level.
                                                    // Listing it anyway is not an option - setRewardId
                                                    // unboxes - and this runs on the scene tick, so one
                                                    // unconfigured blossom would take the whole world down
                                                    // with it. Leave it out of the notify instead.
                                                    return;
                                                }
                                                blossoms.add(
                                                        BlossomBriefInfoOuterClass.BlossomBriefInfo.newBuilder()
                                                                .setSceneId(sceneId)
                                                                .setPos(spawn.getPos().toProto())
                                                                .setResin(20)
                                                                .setMonsterLevel(monsterLevel)
                                                                .setRewardId(previewReward)
                                                                .setCircleCampId(type.getCircleCampId())
                                                                .setRefreshId(
                                                                        type.getBlossomChestId()) // TODO: replace when using actual
                                                                // leylines
                                                                .build());
                                            });
                        });
        return blossoms;
    }

    public int getWorldLevel() {
        return scene.getWorld().getWorldLevel();
    }

    private static Integer getPreviewReward(BlossomType type, int worldLevel) {
        if (type == null || worldLevel < 0 || worldLevel > 9) {
            throw new IllegalArgumentException(
                    "Unsupported blossom type/world level: " + type + "/" + worldLevel);
        }
        // Both kinds have blossomChestId=1 in the native Excel rows. The actual
        // distinction is refreshType (SCOIN vs EXP), shared across cities.
        Integer previewId = null;
        for (var data : GameData.getBlossomRefreshExcelConfigDataMap().values()) {
            if (!type.refreshType().equals(data.getRefreshType())) continue;
            var drops = data.getDropVec();
            // Native Excel has 9 preview tiers (WL0..WL8); WL9 shares the capped WL8 preview.
            int index = worldLevel == 9 ? 8 : worldLevel;
            if (drops == null || drops.length <= index || drops[index] == null
                    || drops[index].getPreviewReward() <= 0) {
                throw new IllegalStateException(
                        "Missing native blossom preview: " + type + " WL" + worldLevel
                                + " refresh=" + data.getId());
            }
            int candidate = drops[index].getPreviewReward();
            if (previewId != null && previewId != candidate) {
                throw new IllegalStateException(
                        "Conflicting native blossom previews: " + type + " WL" + worldLevel);
            }
            previewId = candidate;
        }
        if (previewId == null && MISSING_TYPES_REPORTED.add(type)) {
            Grasscutter.getLogger().error(
                    "Missing native blossom refresh type {} for {}", type.refreshType(), type);
        }
        return previewId;
    }

    public boolean hasRewardChest(EntityGadget chest) {
        synchronized (activeChests) {
            return activeChests.stream().anyMatch(active -> active.getChest() == chest);
        }
    }

    /** Called once the reward chest is fully claimed; ignores chests this manager doesn't own. */
    public void retireChest(EntityGadget chest) {
        synchronized (activeChests) {
            var it = activeChests.iterator();
            while (it.hasNext()) {
                var active = it.next();
                if (active.getChest() == chest) {
                    it.remove();
                    blossomConsumed.add(active.getGadget().getSpawnEntry());
                    recycleGadgetEntity(List.of(active.getGadget()));
                    return;
                }
            }
        }
    }

    /**
     * Claim a completed ley-line flower. Roll the two rewards independently for
     * condensed resin, preflight the complete inventory batch, then charge once.
     * Admission/payment refusal permits retry. Ambiguous delivery failures retain a receipt.
     */
    public List<GameItem> claimReward(Player player, EntityGadget chest, boolean condensed,
            Runnable confirmed) {
        synchronized (activeChests) {
            BlossomActivity active = activeChests.stream()
                    .filter(candidate -> candidate.getChest() == chest)
                    .findFirst().orElse(null);
            if (active == null || active.rewardClaimed(player.getUid())) return null;

            BlossomType type = BlossomType.valueOf(active.getGadget().getGadgetId());
            if (type == null) throw new IllegalStateException("Unknown claimed blossom type");
            int worldLevel = getWorldLevel();
            Map<Integer, Integer> rolled = RewardConfiguration.current().rollBlossom(
                    type.rewardKind(), worldLevel, condensed,
                    java.util.concurrent.ThreadLocalRandom.current());
            var items = rolled.entrySet().stream()
                    .map(entry -> new GameItem(entry.getKey(), entry.getValue()))
                    .toList();
            var resin = player.getResinManager();
            var journal = new RewardGrantJournal();
            var key = new RewardGrantJournal.Key(RewardGrantJournal.Source.BLOSSOM, player.getUid(),
                    "flower." + scene.getId() + "." + active.getGadget().getGroupId() + "."
                            + active.getGadget().getConfigId() + "." + active.rewardOperation(player.getUid()));
            var snapshot = items.stream().map(item -> new RewardGrantJournal.Reward(
                    item.getItemId(), item.getCount(), item.getLevel())).toList();
            var debits = Configuration.GAME_OPTIONS.resinOptions.resinUsage
                    ? List.of(new RewardGrantJournal.Debit(condensed ? 220007 : 106, condensed ? 1 : 20))
                    : List.<RewardGrantJournal.Debit>of();
            boolean accepted = RewardClaimFlow.grant(
                    RewardClaimFlow.receipt(journal, key, snapshot, debits),
                    new RewardClaimFlow.Delivery() {
                        public boolean admit(java.util.function.BooleanSupplier authorize, Runnable claim) {
                            return player.getInventory().addItems(items, ActionReason.OpenWorldBossChest,
                                    InventoryAddPolicy.ALL_OR_NOTHING, authorize, claim).allAccepted();
                        }
                        public void publish() {}
                    }, () -> condensed ? resin.useCondensedResin(1) : resin.useResin(20),
                    () -> {
                        active.confirmReward(player.getUid());
                        confirmed.run();
                    }, () -> active.resetUnpaidReward(player.getUid()));
            if (!accepted) {
                Grasscutter.getLogger().warn("Ley-line claim refused uid={} kind={} receipt={}",
                        player.getUid(), type, key.storageId());
                return null;
            }
            return items;
        }
    }

    public static IntList getRandomMonstersID(int difficulty, int count) {
        IntList result = new IntArrayList();
        List<Integer> monsters =
                GameDepot.getBlossomConfig().getMonsterIdsPerDifficulty().get(difficulty);
        for (int i = 0; i < count; i++) {
            result.add((int) monsters.get(Utils.randomRange(0, monsters.size() - 1)));
        }
        return result;
    }
}
