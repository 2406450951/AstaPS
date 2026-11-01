package emu.grasscutter.game.managers.blossom;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.database.RewardGrantJournal;
import emu.grasscutter.game.reward.RewardClaimFlow;
import emu.grasscutter.config.Configuration;
import emu.grasscutter.data.*;
import emu.grasscutter.data.excels.BlossomCampData;
import emu.grasscutter.data.excels.BlossomRefreshExcelConfigData;
import emu.grasscutter.game.managers.blossom.BlossomGraph.CampNode;
import emu.grasscutter.game.managers.blossom.BlossomGraph.CityGraph;
import emu.grasscutter.game.props.FightProperty;
import emu.grasscutter.scripts.data.SceneBossChest;
import emu.grasscutter.scripts.data.SceneGadget;
import emu.grasscutter.scripts.data.SceneGroup;
import emu.grasscutter.game.inventory.InventoryAddPolicy;
import emu.grasscutter.game.props.ActionReason;
import emu.grasscutter.game.reward.RewardConfiguration;
import emu.grasscutter.game.entity.*;
import emu.grasscutter.game.entity.gadget.GadgetWorktop;
import emu.grasscutter.game.inventory.GameItem;
import emu.grasscutter.game.managers.ResinManager;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.world.*;
import emu.grasscutter.net.proto.*;
import emu.grasscutter.server.packet.send.PacketBlossomBriefInfoNotify;
import java.util.*;

public class BlossomManager {
    static final class Blossom {
        /** Native ley-line kinds. Country availability and counts remain in native Excel. */
        public enum Type {
            WEALTH("BLOSSOM_REFRESH_SCOIN", 1, 70360056, 70210109, "wealth"),
            REVELATION("BLOSSOM_REFRESH_EXP", 3, 70360057, 70210110, "revelation");

            private final String refreshType;
            private final int refreshTypeId;
            private final int gadgetId;
            private final int rewardChestGadgetId;
            private final String rewardKind;

            Type(String refreshType, int refreshTypeId, int gadgetId, int rewardChestGadgetId, String rewardKind) {
                this.refreshType = refreshType;
                this.refreshTypeId = refreshTypeId;
                this.gadgetId = gadgetId;
                this.rewardChestGadgetId = rewardChestGadgetId;
                this.rewardKind = rewardKind;
            }

            public String refreshType() { return refreshType; }
            public int refreshTypeId() { return refreshTypeId; }
            public int gadgetId() { return gadgetId; }
            public int rewardChestGadgetId() { return rewardChestGadgetId; }
            public String rewardKind() { return rewardKind; }

            public static Type fromRefresh(String refreshType) {
                for (var type : values()) if (type.refreshType.equals(refreshType)) return type;
                return null;
            }

            public static Type fromGadget(int gadgetId) {
                for (var type : values()) if (type.gadgetId == gadgetId) return type;
                return null;
            }
        }

        enum State { IDLE, FIGHTING, REWARD }

        final Type type;
        final int slot;
        State state = State.IDLE;

        Blossom(Type type, int slot) {
            this.type = type;
            this.slot = slot;
        }
    }

    private static final Set<Integer> NATIVE_REFRESH_TYPES = Arrays.stream(Blossom.Type.values())
            .map(Blossom.Type::refreshTypeId)
            .collect(java.util.stream.Collectors.toUnmodifiableSet());

    private final Map<Integer, CityGraph> graphs = new HashMap<>();
    private volatile Set<Integer> blossomGroupIds;
    private final Map<RefreshKey, Integer> refreshIds;

    private record RefreshKey(int cityId, Blossom.Type type) {}

    /** Index the native refresh configurations once when the manager is created. */
    private static Map<RefreshKey, Integer> indexRefreshIds() {
        var indexed = new HashMap<RefreshKey, Integer>();
        for (var refresh : GameData.getBlossomRefreshExcelConfigDataMap().values()) {
            var type = Blossom.Type.fromRefresh(refresh.getRefreshType());
            if (type == null) continue;
            var key = new RefreshKey(refresh.getCityId(), type);
            if (indexed.putIfAbsent(key, refresh.getId()) != null)
                throw new IllegalStateException("Duplicate blossom refresh " + key);
        }
        return Map.copyOf(indexed);
    }

    int refreshId(CampNode node) {
        var id = refreshIds.get(new RefreshKey(node.graph.cityId, node.blossom.type));
        if (id == null) throw new IllegalStateException("Missing blossom refresh for " + node.campId);
        return id;
    }

    private CityGraph graphFor(int cityId) {
        return graphs.computeIfAbsent(cityId, city ->
                new CityGraph(city, GameData.getBlossomCampDataMap().values(), NATIVE_REFRESH_TYPES));
    }

    private boolean canUseCamp(CampNode node, Blossom.Type kind) {
        return node.blossom == null && resolveCampGadget(node.campId, kind) != null;
    }

    /** Add only missing slots; never move a surviving flower when eligibility changes. */
    private void selectInitialCamps(
            Scene scene, CityGraph graph, BlossomRefreshExcelConfigData refresh,
            Blossom.Type kind, Collection<Integer> unlockedAreas,
            List<CampNode> selected) {
        var host = scene.getWorld().getHost();
        var starts = new ArrayList<>(graph.initialCamps);
        Collections.shuffle(starts);
        for (int slot = 0; slot < refresh.getRefreshCount(); slot++) {
            // An existing saved run owns its slot even if its Lua resources are unavailable.
            if (host.isBlossomConsumed(refresh.getId(), slot)
                    || host.getBlossomCampId(refresh.getId(), slot) != 0) continue;
            while (!starts.isEmpty()) {
                var chosen = starts.remove(starts.size() - 1);
                if (chosen.blossom != null) continue;
                var gadget = resolveCampGadget(chosen.campId, kind);
                if (gadget == null || gadget.area_id <= 0
                        || !unlockedAreas.contains(gadget.area_id)) continue;
                chosen.blossom = new Blossom(kind, slot);
                selected.add(chosen);
                host.setBlossomCampId(refresh.getId(), slot, chosen.campId);
                break;
            }
        }
    }

    /** Reconcile persisted flowers and initialize only the missing slots. */
    List<CampNode> selectCamps(Scene scene, int rank, Collection<Integer> unlockedAreas) {
        var host = scene.getWorld().getHost();
        var refreshes = GameData.getBlossomRefreshExcelConfigDataMap().values().stream()
                .filter(refresh -> {
                    var type = Blossom.Type.fromRefresh(refresh.getRefreshType());
                    var city = GameData.getCityDataMap().get(refresh.getCityId());
                    return type != null && refresh.getRefreshCount() > 0
                            && BlossomManager.refreshEligible(refresh, rank)
                            && city != null && city.getSceneId() == scene.getId();
                })
                .sorted(Comparator.comparingInt(BlossomRefreshExcelConfigData::getCityId)
                        .thenComparingInt(BlossomRefreshExcelConfigData::getId))
                .toList();
        var selected = new ArrayList<CampNode>();

        for (var refresh : refreshes) {
            var graph = graphFor(refresh.getCityId());
            var kind = Blossom.Type.fromRefresh(refresh.getRefreshType());
            for (int slot = 0; slot < refresh.getRefreshCount(); slot++) {
                if (host.isBlossomConsumed(refresh.getId(), slot)) continue;
                int savedId = host.getBlossomCampId(refresh.getId(), slot);
                if (savedId == 0) continue;
                var node = graph.nodes.get(savedId);
                if (node == null) continue;
                var gadget = resolveCampGadget(savedId, kind);
                if (gadget == null) continue;
                if (node.blossom == null) {
                    node.blossom = new Blossom(kind, slot);
                    if (host.isBlossomRewardReady(refresh.getId(), slot))
                        node.blossom.state = Blossom.State.REWARD;
                }
                if (node.blossom.type != kind || node.blossom.slot != slot) continue;
                selected.add(node);
            }
        }

        for (var refresh : refreshes)
            selectInitialCamps(scene, graphFor(refresh.getCityId()), refresh,
                    Blossom.Type.fromRefresh(refresh.getRefreshType()), unlockedAreas, selected);

        selected.sort(Comparator.comparingInt((CampNode node) -> refreshId(node))
                .thenComparingInt(node -> node.blossom.slot));
        return selected;
    }

    boolean containsGroup(int groupId) {
        var indexed = blossomGroupIds;
        if (indexed == null) {
            indexed = GameData.getBlossomCampDataMap().values().stream()
                    .filter(camp -> camp.getRefreshTypeVec() != null
                            && camp.getRefreshTypeVec().stream().anyMatch(NATIVE_REFRESH_TYPES::contains))
                    .filter(camp -> camp.getNewGroupVec() != null)
                    .flatMap(camp -> camp.getNewGroupVec().stream())
                    .collect(java.util.stream.Collectors.toUnmodifiableSet());
            blossomGroupIds = indexed;
        }
        return indexed.contains(groupId);
    }

    /** Move an existing flower; successors do not have to be in unlocked regions. */
    boolean advanceCamp(Scene scene, CampNode source) {
        var graph = source.graph;
        if (source.blossom == null || source.next.isEmpty()) {
            source.blossom = null;
            return false;
        }

        int refreshId = refreshId(source);
        var blossom = source.blossom;
        source.blossom = null;
        var chosen = graph.findNextCamps(source.next).stream()
                .filter(node -> canUseCamp(node, blossom.type))
                .findFirst().orElse(null);
        if (chosen == null) {
            Grasscutter.getLogger().warn(
                    "No valid ley-line successor city={} kind={} camp={} slot={}",
                    graph.cityId, blossom.type, source.campId, blossom.slot);
            return false;
        }

        blossom.state = Blossom.State.IDLE;
        chosen.blossom = blossom;
        scene.getWorld().getHost().advanceBlossomCampId(
                refreshId, blossom.slot, chosen.campId);
        Grasscutter.getLogger().info("Ley-line camp advanced city={} type={} slot={} {} -> {}",
                graph.cityId, blossom.type, blossom.slot, source.campId, chosen.campId);
        return true;
    }
    private final Scene scene;
    private record CampGadgetKey(int campId, int nativeType) {}
    private final Map<CampGadgetKey, SceneGadget> worktopCache = new HashMap<>();
    private final List<BlossomActivity> blossomActivities = new ArrayList<>();
    private final Map<EntityGadget, CampNode> createdEntity = new IdentityHashMap<>();
    private final Set<CampNode> scriptedLoadedCamps = new HashSet<>();
    private List<CampNode> activeCampCache;
    private int cachedAdventureRank = -1;
    private int cachedDailyReset = -1;
    private Set<Integer> cachedUnlockedAreas = Set.of();
    private int activeSetRevision;
    private boolean mapMarkersDirty;
    private static final double SCRIPTED_SPAWN_RANGE = 400;

    /** Resolve the native worktop. */
    SceneGadget resolveCampGadget(int campId, Blossom.Type kind) {
        var data = GameData.getBlossomCampDataMap().get(campId);
        if (data == null || data.getRefreshTypeVec() == null
                || !data.getRefreshTypeVec().contains(kind.refreshTypeId())
                || data.getNewGroupVec() == null || data.getNewGroupVec().isEmpty()) return null;
        return findWorktop(data, kind);
    }

    /** Resolve original Lua data on demand. */
    private SceneGadget gadget(CampNode node) {
        return resolveCampGadget(node.campId, node.blossom.type);
    }

    /** Lua owns worktop placement; entity creation follows the original script. */
    private SceneGadget findWorktop(BlossomCampData camp, Blossom.Type kind) {
        var key = new CampGadgetKey(camp.getId(), kind.refreshTypeId());
        var cached = worktopCache.get(key);
        if (cached != null) return cached;
        for (int groupId : camp.getNewGroupVec()) {
            SceneGroup group = SceneGroup.of(groupId).load(scene.getId());
            if (group == null || group.gadgets == null) continue;
            var gadget = group.gadgets.values().stream()
                    .filter(item -> item.gadget_id == kind.gadgetId())
                    .min(Comparator.comparingInt(item -> item.config_id)).orElse(null);
            if (gadget != null) {
                worktopCache.put(key, gadget);
                return gadget;
            }
        }
        return null;
    }

    /** Spawn only the camps chosen by BlossomGroups and backed by a real Lua gadget. */
    public void ensureScriptedSpawns() {
        removeInactiveAuthoredOutcrops();
        for (var spawn : activeCamps()) {
            if (scriptedLoadedCamps.contains(spawn)) continue;
            if (scene.getPlayers().stream().noneMatch(player ->
                    player.getPosition().computeDistance(gadget(spawn).pos) <= SCRIPTED_SPAWN_RANGE))
                continue;

            var group = gadget(spawn).group;
            if (group == null) continue;
            restoreClaimableReward(spawn, group);
            // Only the original Lua GROUP_LOAD -> GROUP_REFRESH -> CreateGadget path
            // creates a worktop. The entity-created hook below attaches its camp.
            scene.getScriptManager().ensureBlossomGroup(group);
            scriptedLoadedCamps.add(spawn);
        }
    }

    /**
     * Rebuild the earned reward from the original Lua marker after a process restart.
     * The source worktop is a private identity holder, never added to the scene.
     */
    private void restoreClaimableReward(CampNode spawn, emu.grasscutter.scripts.data.SceneGroup group) {
        int refreshId = refreshId(spawn);
        if (!scene.getWorld().getHost().isBlossomRewardReady(refreshId, spawn.blossom.slot)) return;
        synchronized (blossomActivities) {
            if (blossomActivities.stream().anyMatch(activity ->
                    activity.getGadget().getGroupId() == group.id)) return;
            var source = gadget(spawn);
            var reward = group.gadgets.values().stream()
                    .filter(g -> g.is_blossom_chest)
                    .findFirst().orElse(null);
            if (reward == null) {
                throw new IllegalStateException("Cannot restore earned ley-line group " + group.id);
            }
            var identity = new EntityGadget(scene, source.gadget_id, source.pos);
            identity.setGroupId(group.id);
            identity.setConfigId(source.config_id);
            identity.setMetaGadget(source);
            var activity = new BlossomActivity(identity, spawn);
            if (!activity.restoreFromLua(reward)) {
                throw new IllegalStateException("Invalid earned ley-line group " + group.id);
            }
            blossomActivities.add(activity);
        }
    }

    /** A previously selected, but no longer active, camp cannot leave an interactive gadget. */
    private void removeInactiveAuthoredOutcrops() {
        var seen = new HashSet<String>();
        var toRemove = new ArrayList<GameEntity>();
        for (var entity : List.copyOf(scene.getEntities().values())) {
            if (!(entity instanceof EntityGadget gadget)
                    || !isAuthoredWorktop(gadget.getGroupId(), gadget.getGadgetId())) continue;
            boolean active = isActiveAuthoredGadget(
                    gadget.getGroupId(), gadget.getConfigId(), gadget.getGadgetId());
            String identity = gadget.getGroupId() + ":" + gadget.getConfigId() + ":" + gadget.getGadgetId();
            if (!active || !seen.add(identity)) {
                scene.removeEntity(gadget, VisionTypeOuterClass.VisionType.VisionType_VISION_REMOVE);
                toRemove.add(gadget);
            }
        }
        recycleGadgetEntity(toRemove);
    }

    /** The normal world grid and Lua group loader must obey the same selection. */
    public boolean isActiveSpawn(SpawnDataEntry entry) {
        // Legacy Spawns.json bloom entries are never a second source of camps.
        return Blossom.Type.fromGadget(entry.getGadgetId()) == null;
    }

    /** Report the selected camp's native refresh type to its authored Lua group. */
    public Integer getBlossomRefreshTypeByGroupId(int groupId) {
        for (var spawn : activeCamps()) {
            if (gadget(spawn).group.id != groupId) continue;
            return luaRefreshType(Blossom.Type.fromGadget(gadget(spawn).gadget_id));
        }
        return null;
    }

    static Integer luaRefreshType(Blossom.Type type) {
        if (type == null) return null;
        return type.refreshTypeId();
    }

    /** Lua reports completion; the Java ledger owns the reward and camp rotation. */
    public boolean completeLuaEncounter(int groupId, emu.grasscutter.scripts.data.SceneGadget reward) {
        synchronized (blossomActivities) {
            for (var activity : blossomActivities) {
                if (activity.getGadget().getGroupId() != groupId) continue;
                if (!activity.completeFromLua(reward)) return false;
                scene.getWorld().getHost().setBlossomRewardReady(
                        refreshId(activity.source()), activity.blossom().slot);
                Grasscutter.getLogger().info("Ley-line completed scene={} group={} kills={}/{}",
                        scene.getId(), groupId, activity.nativeKills(), activity.nativeGoal());
                return true;
            }
        }
        return false;
    }

    /** The original Lua trigger route uses the same activity and reward owner. */
    public boolean beginNativeEncounter(EntityGadget source, int monsterGoal) {
        synchronized (blossomActivities) {
            for (var activity : blossomActivities) {
                if (activity.getGadget() != source) continue;
                activity.beginNative(monsterGoal);
                Grasscutter.getLogger().debug("Ley-line encounter prepared scene={} group={} goal={} sceneScriptInit={}",
                        scene.getId(), source.getGroupId(), monsterGoal, scene.getScriptManager().isInit());
                return true;
            }
        }
        return false;
    }

    /** One event per native group monster death, with one final Lua progress event. */
    public int addNativeScheduleProgress(int groupId) {
        synchronized (blossomActivities) {
            for (var activity : blossomActivities) {
                if (activity.getGadget().getGroupId() != groupId) continue;
                boolean finished = activity.addNativeProgress();
                Grasscutter.getLogger().info("Ley-line progress scene={} group={} kills={}/{} finished={}",
                        scene.getId(), groupId, activity.nativeKills(), activity.nativeGoal(), finished);
                if (finished) {
                    scene.getScriptManager().callEvent(new emu.grasscutter.scripts.data.ScriptArgs(
                            groupId, emu.grasscutter.scripts.constants.EventType.EVENT_BLOSSOM_PROGRESS_FINISH));
                }
                return 0;
            }
        }
        return 1;
    }

    public int blossomScheduleState(int groupId) {
        synchronized (blossomActivities) {
            return blossomActivities.stream()
                    .filter(activity -> activity.getGadget().getGroupId() == groupId)
                    .mapToInt(BlossomActivity::scheduleState).findFirst().orElse(1);
        }
    }

    public int setBlossomScheduleState(int groupId, int state) {
        if (state < 1 || state > 3) return 1;
        boolean selected = activeCamps().stream()
                .anyMatch(spawn -> gadget(spawn).group.id == groupId);
        if (!selected) return 1;
        synchronized (blossomActivities) {
            var activity = blossomActivities.stream()
                    .filter(current -> current.getGadget().getGroupId() == groupId)
                    .findFirst().orElse(null);
            // The native Lua requests the transition. The actual phase was
            // already established by encounter start or completion.
            if (state == 1) return activity == null || activity.scheduleState() == 3 ? 0 : 1;
            return activity != null && activity.scheduleState() == state ? 0 : 1;
        }
    }

    private emu.grasscutter.scripts.data.SceneGroup scriptGroupFor(EntityGadget gadget) {
        // Reuse the same Lua environment as the live scene triggers when available.
        var group = scene.getScriptManager().getGroupById(gadget.getGroupId());
        var source = createdEntity.get(gadget);
        return group != null ? group : source == null ? null : gadget(source).group;
    }

    public boolean isNativeCampGroup(int groupId) {
        return groupId != 0 && containsGroup(groupId);
    }

    public boolean isAuthoredWorktop(int groupId, int gadgetId) {
        return isNativeCampGroup(groupId) && Blossom.Type.fromGadget(gadgetId) != null;
    }

    /** Other SceneGroups are never governed by the blossom camp selection. */
    public boolean isActiveAuthoredGroup(int groupId) {
        return !containsGroup(groupId) || activeCamps().stream()
                .anyMatch(spawn -> gadget(spawn).group.id == groupId);
    }

    /** The durable claimable reward excludes another worktop at the same camp. */
    public boolean isRewardPending(int groupId) {
        return activeCamps().stream().anyMatch(spawn ->
                gadget(spawn).group.id == groupId
                        && scene.getWorld().getHost().isBlossomRewardReady(
                                refreshId(spawn), spawn.blossom.slot));
    }

    public boolean isActiveAuthoredGadget(int groupId, int configId, int gadgetId) {
        if (!isAuthoredWorktop(groupId, gadgetId)) return true;
        if (isRewardPending(groupId)) return false;
        boolean selected = activeCamps().stream().anyMatch(entry ->
                gadget(entry).group.id == groupId
                        && gadget(entry).config_id == configId
                        && gadget(entry).gadget_id == gadgetId);
        if (!selected) return false;
        // A delayed GROUP_REFRESH must not recreate the interactive flower
        // while its single encounter is running or its reward remains unclaimed.
        synchronized (blossomActivities) {
            return blossomActivities.stream().noneMatch(activity ->
                    activity.getGadget().getGroupId() == groupId
                            && activity.getGadget().getConfigId() == configId
                            && activity.getGadget().getGadgetId() == gadgetId);
        }
    }

    private List<CampNode> activeCamps() {
        var host = scene.getWorld().getHost();
        int rank = host.getLevel();
        var unlocked = host.getUnlockedSceneAreas(scene.getId());
        if (activeCampCache != null && cachedAdventureRank == rank
                && cachedDailyReset == host.getLastDailyReset()
                && cachedUnlockedAreas.equals(unlocked)) {
            return activeCampCache;
        }

        var old = activeCampCache;
        var selected = selectCamps(scene, rank, unlocked);

        activeCampCache = selected;
        cachedAdventureRank = rank;
        cachedDailyReset = host.getLastDailyReset();
        cachedUnlockedAreas = Set.copyOf(unlocked);
        if (old == null || !old.equals(selected)) {
            activeSetRevision++;
            if (old != null) mapMarkersDirty = true;
        }
        Grasscutter.getLogger().info(
                "Ley-line active scene={} rank={} areas={} camps={} kinds={}",
                scene.getId(), rank, unlocked, selected.size(),
                selected.stream().map(source -> refreshId(source)).distinct().count());
        if (selected.isEmpty() && rank >= 8) {
            for (var refresh : GameData.getBlossomRefreshExcelConfigDataMap().values()) {
                var kind = Blossom.Type.fromRefresh(refresh.getRefreshType());
                var city = GameData.getCityDataMap().get(refresh.getCityId());
                if (kind == null || city == null || city.getSceneId() != scene.getId()
                        || !refreshEligible(refresh, rank)) continue;
                var starts = graphFor(refresh.getCityId()).initialCamps;
                var gadgets = starts.stream().map(node -> resolveCampGadget(node.campId, kind))
                        .filter(Objects::nonNull).toList();
                Grasscutter.getLogger().info(
                        "Ley-line candidates refresh={} type={} heads={} gadgets={} unlockedGadgets={} gadgetAreas={}",
                        refresh.getId(), kind, starts.size(), gadgets.size(),
                        gadgets.stream().filter(g -> unlocked.contains(g.area_id)).count(),
                        gadgets.stream().map(g -> g.area_id).distinct().toList());
            }
        }
        return selected;
    }

    /** Used by the normal grid loader to notice eligibility changes without player movement. */
    public int getActiveSetRevision() {
        activeCamps();
        return activeSetRevision;
    }

    /** Temporary adventure-rank gates for Mondstadt and Liyue. */
    static boolean refreshEligible(BlossomRefreshExcelConfigData refresh, int adventureRank) {
        return switch (refresh.getCityId()) {
            case 1 -> adventureRank >= (Blossom.Type.fromRefresh(refresh.getRefreshType())
                    == Blossom.Type.WEALTH ? 12 : 8);
            case 2 -> adventureRank >= 18;
            default -> true;
        };
    }

    public BlossomManager(Scene scene) {
        this.scene = scene;
        this.refreshIds = indexRefreshIds();
    }

    /** Attach selected native worktops when they enter the scene. */
    public void onSceneEntityCreated(GameEntity entity) {
        if (entity instanceof EntityGadget gadget
                && isAuthoredWorktop(gadget.getGroupId(), gadget.getGadgetId())) {
            initBlossom(gadget);
        }
    }

    public void onTick() {
        activeCamps();
        // Scene.checkGroups() already loads nearby authored outcrops in script mode.
        // The normal spawn-grid path needs the same loader.
        if (!scene.getScriptManager().isInit()) ensureScriptedSpawns();
        if (mapMarkersDirty) notifyIcon();
        synchronized (blossomActivities) {
            for (var active : blossomActivities) {
                if (!active.rewardReady()) continue;
                EntityGadget chest = active.getChest();
                scene.addEntity(chest);
                Grasscutter.getLogger().info(
                        "Ley-line reward flower spawned scene={} gadget={} source={} nativeKills={}/{}",
                        scene.getId(), chest.getGadgetId(),
                        active.getGadget().getGadgetId(),
                        active.nativeKills(), active.nativeGoal());
            }
        }
    }

    /** Entity unload is separate from activity completion and map-marker changes. */
    public void recycleGadgetEntity(List<GameEntity> entities) {
        for (var entity : entities) {
            if (!(entity instanceof EntityGadget gadget)
                    || Blossom.Type.fromGadget(gadget.getGadgetId()) == null) continue;
            var source = createdEntity.remove(gadget);
            if (source != null) scriptedLoadedCamps.remove(source);
        }
    }

    private long nativeLiveMonsterCount(int groupId, emu.grasscutter.scripts.data.SceneGroup group) {
        return scene.getEntities().values().stream()
                .filter(entity -> entity instanceof emu.grasscutter.game.entity.EntityMonster
                        && entity.getGroupId() == groupId
                        && (group == null || group.monsters == null
                                || group.monsters.containsKey(entity.getConfigId())))
                .count();
    }

    public void initBlossom(EntityGadget gadget) {
        if (createdEntity.containsKey(gadget)) {
            return;
        }
        var id = gadget.getGadgetId();
        if (Blossom.Type.fromGadget(id) == null) {
            return;
        }
        var camp = activeCamps().stream()
                .filter(source -> gadget(source).group.id == gadget.getGroupId()
                        && gadget(source).config_id == gadget.getConfigId()
                        && gadget(source).gadget_id == gadget.getGadgetId())
                .findFirst().orElse(null);
        if (camp == null) return;
        // Source gadget rows may carry a generic type under 7.1; a ley-line outcrop
        // always needs a worktop to start its combat challenge.
        GadgetWorktop gadgetWorktop = gadget.getContent() instanceof GadgetWorktop worktop
                ? worktop : new GadgetWorktop(gadget);
        gadget.replaceContent(gadgetWorktop);
        gadget.setState(204);
        gadgetWorktop.addWorktopOptions(new int[] {187});
        gadgetWorktop.setOnSelectWorktopOptionEvent(
                (GadgetWorktop context, int option) -> {
                    if (option != 187) return false;
                    EntityGadget source = context.getGadget();
                    BlossomActivity activity = new BlossomActivity(source, camp);
                    synchronized (blossomActivities) {
                        if (blossomActivities.stream().anyMatch(current ->
                                current.getGadget() == source)) return false;
                        blossomActivities.add(activity);
                    }

                    // Dispatch only the original scene's select trigger.
                    // Keep the worktop available if the native script fails.
                    var group = scriptGroupFor(source);
                    int monsterGoal = group == null || group.monsters == null ? 0 : group.monsters.size();
                    boolean prepared = beginNativeEncounter(source, monsterGoal);
                    boolean selected = prepared && scene.getScriptManager().dispatchBlossomSelect(
                            source.getGroupId(), source.getConfigId(), option);
                    if (!selected) {
                        Grasscutter.getLogger().warn(
                                "Ley-line encounter rejected scene={} group={} config={} prepared={} liveMonsters={}",
                                scene.getId(), source.getGroupId(), source.getConfigId(), prepared,
                                nativeLiveMonsterCount(source.getGroupId(), group));
                        activity.abort();

                        synchronized (blossomActivities) {
                            blossomActivities.remove(activity);
                        }
                        // The activity owns its transient Lua schedule state.
                        source.updateState(204);
                        context.addWorktopOptions(new int[] {187});
                        return false;
                    }
                    Grasscutter.getLogger().info(
                            "Ley-line encounter active scene={} group={} config={} liveMonsters={} deathTriggers={}",
                            scene.getId(), source.getGroupId(), source.getConfigId(),
                            nativeLiveMonsterCount(source.getGroupId(), group),
                            scene.getScriptManager().getTriggersByEvent(
                                    emu.grasscutter.scripts.constants.EventType.EVENT_ANY_MONSTER_DIE).stream()
                                    .filter(trigger -> trigger.getCurrentGroup().id == source.getGroupId())
                                    .count());
                    source.updateState(201);
                    // Scene.selectWorktopOptionWith removes the selected gadget exactly once.
                    return true;
                });
        createdEntity.put(gadget, camp);
        notifyIcon();
    }

    /** Each player receives only the outcrops unlocked at their current Adventure Rank. */
    public void notifyIcon() {
        activeCamps();
        mapMarkersDirty = false;
        scene.getPlayers().forEach(this::notifyIcon);
    }

    /** Sync map markers after EnterSceneDone, including when no outcrop is nearby. */
    public void notifyIcon(Player player) {
        player.sendPacket(new PacketBlossomBriefInfoNotify(buildBriefInfos(player)));
    }

    private List<BlossomBriefInfoOuterClass.BlossomBriefInfo> buildBriefInfos(Player player) {
        final int wl = getWorldLevel();
        final int worldLevel = (wl < 0) ? 0 : ((wl > 9) ? 9 : wl);
        final var worldLevelData = GameData.getWorldLevelDataMap().get(worldLevel);
        final int monsterLevel = (worldLevelData != null) ? worldLevelData.getMonsterLevel() : 1;
        var blossoms = new ArrayList<BlossomBriefInfoOuterClass.BlossomBriefInfo>();
        for (var spawn : activeCamps()) {
            var type = Blossom.Type.fromGadget(gadget(spawn).gadget_id);
            int refreshId = refreshId(spawn);
            int campId = spawn.campId;
            var refresh = GameData.getBlossomRefreshExcelConfigDataMap().get(refreshId);
            if (type == null || refresh == null
                    || !refreshEligible(refresh, player.getLevel()))
                continue;
            int previewReward = getPreviewReward(refresh, worldLevel);
            blossoms.add(BlossomBriefInfoOuterClass.BlossomBriefInfo.newBuilder()
                    .setSceneId(scene.getId())
                    .setPos(gadget(spawn).pos.toProto())
                    .setResin(20)
                    .setMonsterLevel(monsterLevel)
                    .setRewardId(previewReward)
                    .setCircleCampId(campId)
                    .setRefreshId(refreshId)
                    .build());
        }
        return blossoms;
    }

    public int getWorldLevel() {
        return scene.getWorld().getWorldLevel();
    }

    /** Read the map preview from this flower's own country-specific refresh row. */
    static int getPreviewReward(BlossomRefreshExcelConfigData refresh, int worldLevel) {
        if (refresh == null || worldLevel < 0 || worldLevel > 9) {
            throw new IllegalArgumentException("Invalid blossom refresh or world level");
        }
        // Native Excel has WL0..WL8 previews; WL9 uses the capped WL8 value.
        int index = Math.min(worldLevel, 8);
        var drops = refresh.getDropVec();
        if (drops == null || drops.length <= index || drops[index] == null
                || drops[index].getPreviewReward() <= 0) {
            throw new IllegalStateException(
                    "Missing blossom preview: city=" + refresh.getCityId()
                            + " refresh=" + refresh.getId() + " WL" + worldLevel);
        }
        return drops[index].getPreviewReward();
    }

    public boolean hasRewardChest(EntityGadget chest) {
        synchronized (blossomActivities) {
            return blossomActivities.stream().anyMatch(active -> active.isClaimableChest(chest));
        }
    }

    /** Called once the reward chest is fully claimed; ignores chests this manager doesn't own. */
    public void retireChest(EntityGadget chest) {
        synchronized (blossomActivities) {
            var it = blossomActivities.iterator();
            while (it.hasNext()) {
                var active = it.next();
                if (active.isClaimableChest(chest)) {
                    var source = active.source();
                    if (source != null) {
                        int refreshId = refreshId(active.source());
                        int slot = active.blossom().slot;
                        if (!advanceCamp(scene, source)) {
                            scene.getWorld().getHost().setBlossomConsumed(refreshId, slot);
                        }
                        activeCampCache = null;
                        activeSetRevision++;
                        // Unregister the completed group's Lua triggers before entering its next camp.
                        if (scene.getScriptManager().getGroupInstanceById(
                                active.getGadget().getGroupId()) != null)
                            scene.unloadGroup(null, active.getGadget().getGroupId());
                    }
                    it.remove();
                    createdEntity.remove(active.getGadget());
                    // A restored reward uses a private identity gadget with no live worktop.
                    // Its native camp must still be released when the claim rotates.
                    if (source != null) scriptedLoadedCamps.remove(source);
                    ensureScriptedSpawns();
                    notifyIcon();
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
    record ClaimCost(int rolls, int itemId, int amount) {}

    /**
     * Global 7.1 GadgetInteractReq field 14 selects one (20 resin) or two (40 resin)
     * original-resin claims. Both use ResinCostType_NORMAL on the wire.
     */
    static ClaimCost claimCost(
            ResinCostTypeOuterClass.ResinCostType costType, int buyCount, int normalClaimCount) {
        return switch (costType) {
            case ResinCostType_NONE, ResinCostType_NORMAL ->
                    normalClaimCount == 1 || normalClaimCount == 2
                            ? new ClaimCost(normalClaimCount, 106, 20 * normalClaimCount)
                            : null;
            case ResinCostType_CONDENSE -> new ClaimCost(3, 220007, 1);
            case ResinCostType_FRAGILE -> new ClaimCost(3, 107009, 1);
            case ResinCostType_TIMING -> new ClaimCost(3, 107012, 1);
            case ResinCostType_HCOIN -> buyCount < ResinManager.MAX_RESIN_BUYING_COUNT
                    ? new ClaimCost(3, 201, ResinManager.HCOIN_NUM_TO_BUY_RESIN[buyCount]) : null;
            default -> null;
        };
    }

    /** Only a completed gadget interaction can authorize a resin charge or payout. */
    public static boolean isClaimFinish(GadgetInteractReqOuterClass.GadgetInteractReq req) {
        // Global 7.1 encodes FINISH as the proto3 default op type. A missing
        // op field alone cannot distinguish FINISH from an empty request.
        // Require the target entity as well; an empty payload is never a claim.
        return req != null && req.getGadgetEntityId() > 0 && req.getOpType()
                == InterOpTypeOuterClass.InterOpType.InterOpType_INTER_OP_FINISH;
    }

    /** Global 7.1 original-resin amount selector: GadgetInteractReq field 14. */
    static int normalClaimCount(GadgetInteractReqOuterClass.GadgetInteractReq req) {
        return (int) req.getField(req.getDescriptorForType().findFieldByNumber(14));
    }

    public List<GameItem> claimReward(Player player, EntityGadget chest,
            GadgetInteractReqOuterClass.GadgetInteractReq req, Runnable confirmed) {
        if (!isClaimFinish(req)) return null;
        synchronized (blossomActivities) {
            BlossomActivity active = blossomActivities.stream()
                    .filter(candidate -> candidate.isClaimableChest(chest))
                    .findFirst().orElse(null);
            if (active == null || active.rewardClaimed(player.getUid())) return null;

            Blossom.Type type = Blossom.Type.fromGadget(active.getGadget().getGadgetId());
            if (type == null) throw new IllegalStateException("Unknown claimed blossom type");
            var costType = req.getResinCostType();
            int normalClaimCount = normalClaimCount(req);
            var cost = claimCost(costType, player.getResinBuyCount(), normalClaimCount);
            if (cost == null) {
                Grasscutter.getLogger().warn(
                        "Ley-line claim rejected uid={} resinCostType={} field14={}",
                        player.getUid(), costType, normalClaimCount);
                return null;
            }
            int resinBefore = player.getProperty(emu.grasscutter.game.props.PlayerProperty.PROP_PLAYER_RESIN);
            int refreshId = refreshId(active.source());
            var host = scene.getWorld().getHost();
            // BSON-safe key also identifies the claim attempt in the player's persisted record.
            String runId = host.getUid() + ":" + refreshId + ":" + active.blossom().slot + ":"
                    + host.getBlossomIncarnation(refreshId, active.blossom().slot);
            var resin = player.getResinManager();
            var journal = new RewardGrantJournal();
            var key = new RewardGrantJournal.Key(RewardGrantJournal.Source.BLOSSOM, player.getUid(),
                    "flower." + scene.getId() + "." + active.getGadget().getGroupId() + "."
                            + active.getGadget().getConfigId() + "." + runId + "."
                            + player.getBlossomClaimAttempt(runId));
            if (journal.completed(key)) {
                // A previous delivery survived while the scene/group was reloaded.
                // Retire it without charging resin or announcing another reward.
                active.confirmReward(player.getUid());
                confirmed.run();
                return List.of();
            }
            int worldLevel = getWorldLevel();
            Map<Integer, Integer> rolled = RewardConfiguration.current().rollBlossom(
                    type.rewardKind(), worldLevel, cost.rolls(),
                    java.util.concurrent.ThreadLocalRandom.current());
            var items = rolled.entrySet().stream()
                    .map(entry -> new GameItem(entry.getKey(), entry.getValue()))
                    .toList();
            var snapshot = items.stream().map(item -> new RewardGrantJournal.Reward(
                    item.getItemId(), item.getCount(), item.getLevel())).toList();
            var debits = Configuration.GAME_OPTIONS.resinOptions.resinUsage
                    ? List.of(new RewardGrantJournal.Debit(cost.itemId(), cost.amount()))
                    : List.<RewardGrantJournal.Debit>of();
            boolean accepted = RewardClaimFlow.grant(
                    RewardClaimFlow.receipt(journal, key, snapshot, debits),
                    new RewardClaimFlow.Delivery() {
                        public boolean admit(java.util.function.BooleanSupplier authorize, Runnable claim) {
                            return player.getInventory().addItems(items, ActionReason.OpenWorldBossChest,
                                    InventoryAddPolicy.ALL_OR_NOTHING, authorize, claim).allAccepted();
                        }
                        public void publish() {}
                    }, () -> switch (costType) {
                        case ResinCostType_CONDENSE -> resin.useCondensedResin(1);
                        case ResinCostType_FRAGILE, ResinCostType_TIMING ->
                                !Configuration.GAME_OPTIONS.resinOptions.resinUsage
                                        || player.getInventory().payItem(cost.itemId(), cost.amount());
                        case ResinCostType_HCOIN -> resin.payHcoinRewardClaim();
                        default -> resin.useResin(cost.amount());
                    },
                    () -> {
                        active.confirmReward(player.getUid());
                        confirmed.run();
                    }, () -> player.advanceBlossomClaimAttempt(runId));
            if (!accepted) {
                Grasscutter.getLogger().warn("Ley-line claim refused uid={} kind={} receipt={}",
                        player.getUid(), type, key.storageId());
                return null;
            }
            Grasscutter.getLogger().info(
                    "Ley-line claim settled uid={} kind={} resinCostType={} rolls={} debitItem={} debitAmount={} resinBefore={} resinAfter={} rewards={}",
                    player.getUid(), type, costType, cost.rolls(), cost.itemId(), cost.amount(),
                    resinBefore,
                    player.getProperty(emu.grasscutter.game.props.PlayerProperty.PROP_PLAYER_RESIN),
                    rolled);
            return items;
        }
    }


    /** Per-encounter server entities and reward receipt, driven by Lua events. */
    static final class BlossomActivity {
        private final EntityGadget gadget;
        private final CampNode source;
        private final Blossom blossom;
        private int nativeMonsterGoal;
        private int nativeMonsterProgress;
        private boolean nativeProgressReported;
        private EntityGadget chest;
        private SceneGadget nativeRewardGadget;
        private final Set<Integer> rewardClaimants = new HashSet<>();

        boolean rewardClaimed(int uid) { return rewardClaimants.contains(uid); }
        void confirmReward(int uid) { rewardClaimants.add(uid); }

        public BlossomActivity(EntityGadget gadget, CampNode source) {
            this.gadget = gadget;
            this.source = source;
            this.blossom = source.blossom;
        }

        CampNode source() { return source; }
        Blossom blossom() { return blossom; }
        private Blossom.State state() { return blossom.state; }

        boolean rewardReady() {
            return state() == Blossom.State.REWARD && chest == null;
        }
        boolean isClaimableChest(EntityGadget entity) {
            return state() == Blossom.State.REWARD && chest == entity;
        }
        /** Lua's schedule stage is a projection of the flower lifecycle. */
        int scheduleState() {
            return switch (state()) {
                case IDLE -> 1;
                case FIGHTING -> 2;
                case REWARD -> 3;
            };
        }

        /** Use the native Lua group monster definitions as its scripted progress target. */
        void beginNative(int monsterGoal) {
            blossom.state = Blossom.State.FIGHTING;
            nativeMonsterGoal = monsterGoal;
            nativeMonsterProgress = 0;
            nativeProgressReported = false;
        }

        /** Native Lua reports each group monster death; complete the progress event once. */
        boolean addNativeProgress() {
            if (nativeMonsterGoal <= 0 || nativeProgressReported || state() != Blossom.State.FIGHTING) return false;
            if (++nativeMonsterProgress < nativeMonsterGoal) return false;
            nativeProgressReported = true;
            return true;
        }

        void abort() {
            blossom.state = Blossom.State.IDLE;
        }

        int nativeKills() { return nativeMonsterProgress; }
        int nativeGoal() { return nativeMonsterGoal; }

        /** Restore a previously earned reward from its original Lua marker. */
        boolean restoreFromLua(SceneGadget rewardDefinition) {
            if (state() != Blossom.State.REWARD
                    || rewardDefinition == null || !rewardDefinition.is_blossom_chest) return false;
            nativeRewardGadget = rewardDefinition;
            return true;
        }

        /** Mark completion once, after the Lua script has reached its own success condition. */
        boolean completeFromLua(SceneGadget rewardDefinition) {
            if (state() != Blossom.State.FIGHTING || rewardDefinition == null
                    || !rewardDefinition.is_blossom_chest) return false;
            nativeRewardGadget = rewardDefinition;
            blossom.state = Blossom.State.REWARD;
            return true;
        }

        public EntityGadget getGadget() {
            return gadget;
        }

        /** Build the claimable flower using the Lua reward marker and configured chest type. */
        public EntityGadget getChest() {
            if (chest == null) {
                if (nativeRewardGadget == null || state() != Blossom.State.REWARD) {
                    throw new IllegalStateException("Native Lua has not declared a blossom reward");
                }
                EntityGadget rewardGadget = new EntityGadget(
                        gadget.getScene(),
                        Blossom.Type.fromGadget(gadget.getGadgetId()).rewardChestGadgetId(),
                        nativeRewardGadget.pos);
                rewardGadget.setGroupId(gadget.getGroupId());
                rewardGadget.setConfigId(nativeRewardGadget.config_id);
                rewardGadget.getRotation().set(nativeRewardGadget.rot);
                SceneGadget metaGadget = new SceneGadget();
                metaGadget.config_id = nativeRewardGadget.config_id;
                metaGadget.pos = nativeRewardGadget.pos;
                metaGadget.rot = nativeRewardGadget.rot;
                metaGadget.group = nativeRewardGadget.group;
                metaGadget.is_blossom_chest = true;
                metaGadget.boss_chest = new SceneBossChest();
                metaGadget.boss_chest.resin = 20;
                rewardGadget.setFightProperty(FightProperty.FIGHT_PROP_BASE_HP, Float.POSITIVE_INFINITY);
                rewardGadget.setFightProperty(FightProperty.FIGHT_PROP_CUR_HP, Float.POSITIVE_INFINITY);
                rewardGadget.setFightProperty(FightProperty.FIGHT_PROP_MAX_HP, Float.POSITIVE_INFINITY);
                rewardGadget.setMetaGadget(metaGadget);
                rewardGadget.buildContent();
                chest = rewardGadget;
            }
            return chest;
        }
    }
}
