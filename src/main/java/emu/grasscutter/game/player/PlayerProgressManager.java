package emu.grasscutter.game.player;

import static emu.grasscutter.config.Configuration.GAME_OPTIONS;
import static emu.grasscutter.scripts.constants.EventType.EVENT_UNLOCK_TRANS_POINT;

import emu.grasscutter.data.GameData;
import emu.grasscutter.data.binout.ScenePointEntry;
import emu.grasscutter.data.excels.OpenStateData;
import emu.grasscutter.data.excels.OpenStateData.OpenStateCondType;
import emu.grasscutter.game.props.ActionReason;
import emu.grasscutter.game.quest.enums.*;
import emu.grasscutter.net.proto.RetcodeOuterClass.Retcode;
import emu.grasscutter.scripts.data.ScriptArgs;
import emu.grasscutter.server.packet.send.*;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;

// @Entity
public final class PlayerProgressManager extends BasePlayerDataManager {
    /******************************************************************************************************************
     ******************************************************************************************************************
     * OPEN STATES
     ******************************************************************************************************************
     *****************************************************************************************************************/

    // Previously blacklisted OPEN_STATE_LIMIT_REGION_GLOBAL (48). Remote LunaGC forces it ON when
    // questing is disabled; keeping it blacklisted prevented default unlock and contributed to
    // region gating. Empty set = match remote unlock behaviour.
    public static final Set<Integer> BLACKLIST_OPEN_STATES = Set.of();

    public static final Set<Integer> IGNORED_OPEN_STATES =
            Set.of(
                    1404 // OPEN_STATE_MENGDE_INFUSEDCRYSTAL, causes quest 'Mine Craft' to be given to the
                    // player at the start of the game.
                    // This should be removed when city reputation is implemented.
                    );

    /**
     * Login fallback states, only for genuine resource defaults plus the Paimon menu needed
     * for chat. Quest-, level- and region-gated features use the runtime condition checker.
     */
    public static final Set<Integer> DEFAULT_OPEN_STATES =
            GameData.getOpenStateList().stream()
                    .filter(PlayerProgressManager::isDefaultOpenState)
                    .map(OpenStateData::getId)
                    .filter(id -> !BLACKLIST_OPEN_STATES.contains(id))
                    .filter(id -> !IGNORED_OPEN_STATES.contains(id))
                    .collect(Collectors.toSet());

    static boolean isDefaultOpenState(OpenStateData state) {
        return (state.isDefaultState() && !state.isAllowClientOpen())
                || state.getId() == 1;
    }

    /**
     * Automatic progression needs real server-owned conditions or a genuine default.
     * An absent/unknown condition must not unlock a non-default feature at login.
     */
    static boolean isAutomaticOpenStateCandidate(OpenStateData state) {
        return !state.isAllowClientOpen()
                && (state.isDefaultState()
                        || (state.getCond() != null
                                && state.getCond().stream().anyMatch(c -> c.getCondType() != null)));
    }

    public PlayerProgressManager(Player player) {
        super(player);
    }

    record TransPointUnlockTransition(boolean changed, boolean newlyUnlocked, boolean wasForceLocked) {}

    static boolean applyQuestPointLock(Set<Integer> forceLockedPoints, int pointId) {
        return forceLockedPoints.add(pointId);
    }

    static TransPointUnlockTransition applyTransPointUnlock(
            Set<Integer> unlockedPoints, Set<Integer> forceLockedPoints, int pointId) {
        boolean wasForceLocked = forceLockedPoints.remove(pointId);
        boolean newlyUnlocked = unlockedPoints.add(pointId);
        return new TransPointUnlockTransition(
                wasForceLocked || newlyUnlocked, newlyUnlocked, wasForceLocked);
    }

    /**********
     * Handler for player login.
     **********/
    public void onPlayerLogin() {
        // Try unlocking open states on player login. This handles accounts where unlock conditions were
        // already met before certain open state unlocks were implemented.
        this.tryUnlockOpenStates(false);

        if (!GAME_OPTIONS.questing.enabled) {
            // Retain the existing starter Mondstadt navigation area when questing is disabled.
            // This map unlock is separate from the OpenState mass-unlock option.
            this.player.getUnlockedScenePoints(3).add(7);
            this.player.getUnlockedSceneAreas(3).add(1);
        }

        if (GAME_OPTIONS.unlockAllOpenStatesOnLogin) {
            // Sandbox opt-in: preserve the former bulk unlock, but never tie it to questing.
            // Do this before OpenStateUpdateNotify so the login snapshot is complete.
            this.setOpenState(47, 1, false);
            this.setOpenState(48, 1, false);
            this.setOpenState(1101, 1, false);
            this.setOpenState(1102, 1, false);

            int unlocked = 0;
            for (var openState : GameData.getOpenStateList()) {
                int id = openState.getId();
                if (BLACKLIST_OPEN_STATES.contains(id) || IGNORED_OPEN_STATES.contains(id)) {
                    continue;
                }
                if (this.getOpenState(id) == 0) {
                    unlocked++;
                }
                // Always force 1 into the player map so OpenStateUpdateNotify below is complete
                // even for accounts that already had sparse/partial maps.
                this.player.getOpenStates().put(id, 1);
            }
            emu.grasscutter.Grasscutter.getLogger()
                    .debug(
                            "Configured OpenState bulk unlock uid={} mapSize={} newlySet={}",
                            this.player.getUid(),
                            this.player.getOpenStates().size(),
                            unlocked);
        }

        // Send notify after optional mass unlock so the login snapshot is complete.
        player.getSession().send(new PacketOpenStateUpdateNotify(this.player));

        // Offerings such as the Sacred Sakura: send PlayerOfferingDataNotify on login, otherwise the
        // client never shows the F prompt.
        try {
            emu.grasscutter.game.entity.gadget.OfferingHelper.onPlayerLogin(this.player);
        } catch (Throwable ignored) {
        }

        // Add statue quests if necessary.
        this.addStatueQuestsOnLogin();

        // Ensure spring volume props exist before first statue EnterTrans (tip/heal path).
        try {
            var sots = this.player.getSotsManager();
            if (sots.getMaxVolume() <= 0) {
                sots.setMaxVolume(Math.min(8500000, 5000 * 100)); // 5 statues worth baseline
            }
            if (sots.getCurrentVolume() <= 0) {
                sots.setCurrentVolume(sots.getMaxVolume());
            }
        } catch (Throwable ignored) {
        }
    }

    /**********
     * Direct getters and setters for open states.
     **********/
    public int getOpenState(int openState) {
        return this.player.getOpenStates().getOrDefault(openState, 0);
    }

    private void setOpenState(int openState, int value, boolean sendNotify) {
        int previousValue = this.player.getOpenStates().getOrDefault(openState, -1 /* non-existent */);

        if (value != previousValue) {
            this.player.getOpenStates().put(openState, value);

            this.player
                    .getQuestManager()
                    .queueEvent(QuestCond.QUEST_COND_OPEN_STATE_EQUAL, openState, value);

            if (sendNotify) {
                player.getSession().send(new PacketOpenStateChangeNotify(openState, value));
            }
        }
    }

    private void setOpenState(int openState, int value) {
        this.setOpenState(openState, value, true);
    }

    /**********
     * Condition checking for setting open states.
     **********/
    private boolean areConditionsMet(OpenStateData openState) {
        // Check all conditions and test if at least one of them is violated.
        for (var condition : openState.getCond()) {
            switch (condition.getCondType()) {
                    // For level conditions, check if the player has reached the necessary level.
                case OPEN_STATE_COND_PLAYER_LEVEL -> {
                    if (this.player.getLevel() < condition.getParam()) {
                        return false;
                    }
                }
                case OPEN_STATE_COND_QUEST -> {
                    // check sub quest id for quest finished met requirements
                    var quest = this.player.getQuestManager().getQuestById(condition.getParam());
                    if (quest == null || quest.getState() != QuestState.QUEST_STATE_FINISHED) {
                        return false;
                    }
                }
                case OPEN_STATE_COND_PARENT_QUEST -> {
                    // check main quest id for quest finished met requirements
                    // TODO not sure if its having or finished quest
                    var mainQuest = this.player.getQuestManager().getMainQuestById(condition.getParam());
                    if (mainQuest == null
                            || mainQuest.getState() != ParentQuestState.PARENT_QUEST_STATE_FINISHED) {
                        return false;
                    }
                }
                    // ToDo: Implement.
                case OPEN_STATE_OFFERING_LEVEL, OPEN_STATE_CITY_REPUTATION_LEVEL -> {
                    // Unsupported offering and reputation requirements must not auto-unlock.
                    return false;
                }
            }
        }

        // Done. If we didn't find any violations, all conditions are met.
        return true;
    }

    /**********
     * Setting open states from the client (via `SetOpenStateReq`).
     **********/
    public void setOpenStateFromClient(int openState, int value) {
        // Get the data for this open state.
        OpenStateData data = GameData.getOpenStateDataMap().get(openState);
        if (data == null) {
            this.player.sendPacket(new PacketSetOpenStateRsp(Retcode.RET_FAIL));
            return;
        }

        // Make sure that this is an open state that the client is allowed to set,
        // and that it doesn't have any further conditions attached.
        if (!data.isAllowClientOpen() || !this.areConditionsMet(data)) {
            this.player.sendPacket(new PacketSetOpenStateRsp(Retcode.RET_FAIL));
            return;
        }

        // Set.
        this.setOpenState(openState, value);
        this.player.sendPacket(new PacketSetOpenStateRsp(openState, value));
    }

    /** This force sets an open state, ignoring all conditions and permissions */
    public void forceSetOpenState(int openState, int value) {
        this.setOpenState(openState, value);
    }

    /**********
     * Triggered unlocking of open states (unlock states whose conditions have been met.)
     **********/
    public void tryUnlockOpenStates(boolean sendNotify) {
        // Get list of open states that are not yet unlocked.
        var lockedStates =
                GameData.getOpenStateList().stream()
                        .filter(s -> this.player.getOpenStates().getOrDefault(s.getId(), 0) == 0)
                        .toList();

        // Try unlocking all of them.
        for (var state : lockedStates) {
            // Client-owned, conditionless non-default and unsupported states stay locked.
            if (isAutomaticOpenStateCandidate(state)
                    && this.areConditionsMet(state)
                    && !BLACKLIST_OPEN_STATES.contains(state.getId())
                    && !IGNORED_OPEN_STATES.contains(state.getId())) {
                this.setOpenState(state.getId(), 1, sendNotify);
            }
        }
    }

    public void tryUnlockOpenStates() {
        this.tryUnlockOpenStates(true);
    }

    /******************************************************************************************************************
     ******************************************************************************************************************
     * MAP AREAS AND POINTS
     ******************************************************************************************************************
     *****************************************************************************************************************/
    private void addStatueQuestsOnLogin() {
        // Get all currently existing subquests for the "unlock all statues" main quest.
        var statueMainQuest = GameData.getMainQuestDataMap().get(303);
        if (statueMainQuest == null || statueMainQuest.getSubQuests() == null) {
            this.seedUnlockedStatuesFromLegacyUnlockAll();
            this.forgeAllStatueTalkGates();
            return;
        }
        var statueSubQuests = statueMainQuest.getSubQuests();

        // Add the main statue quest if it isn't active yet.
        var statueGameMainQuest = this.player.getQuestManager().getMainQuestById(303);
        if (statueGameMainQuest == null) {
            this.player.getQuestManager().addQuest(30302);
            statueGameMainQuest = this.player.getQuestManager().getMainQuestById(303);
        }

        // Migrate: old GetScenePointRsp unlocked-all statues visually without writing the set.
        // Seed unlocked statues once so already-lit pillars stay lit; locked ones stay unlockable.
        this.seedUnlockedStatuesFromLegacyUnlockAll();
        // Rebuild map fog from unlocked points (statues + waypoints) - one area per unlock.
        this.syncSceneAreasFromUnlockedPoints(3);

        // Always mark statue Talk gates FINISHED so goddess F / heal / offer need no questing.
        // Do NOT call GameQuest.finish(): finishExec hangs login / white-screens the client.
        int finished = 0;
        try {
            if (statueGameMainQuest != null) {
                for (var subData : statueSubQuests) {
                    var subGameQuest = statueGameMainQuest.getChildQuestById(subData.getSubId());
                    if (subGameQuest == null) {
                        this.player.getQuestManager().addQuest(subData.getSubId());
                        subGameQuest = statueGameMainQuest.getChildQuestById(subData.getSubId());
                    } else if (subGameQuest.getState() == QuestState.QUEST_STATE_UNSTARTED) {
                        this.player.getQuestManager().addQuest(subData.getSubId());
                        subGameQuest = statueGameMainQuest.getChildQuestById(subData.getSubId());
                    }
                    if (subGameQuest != null
                            && subGameQuest.getState() != QuestState.QUEST_STATE_FINISHED) {
                        subGameQuest.setState(QuestState.QUEST_STATE_FINISHED);
                        subGameQuest.setFinishTime(emu.grasscutter.utils.Utils.getCurrentSeconds());
                        subGameQuest.save();
                        finished++;
                    }
                }
            }

            // Only bypass the starter-statue quest when the normal quest runtime is unavailable.
            // Pre-finishing 35205 during normal questing materializes main quest 352 before the
            // 351 -> 352 handoff and leaves its opening subquest unstarted.
            if (shouldBypassStarterStatueQuest()) {
                var q = this.player.getQuestManager().getQuestById(35205);
                if (q == null) {
                    this.player.getQuestManager().addQuest(35205);
                    q = this.player.getQuestManager().getQuestById(35205);
                }
                if (q != null && q.getState() != QuestState.QUEST_STATE_FINISHED) {
                    q.setState(QuestState.QUEST_STATE_FINISHED);
                    q.setFinishTime(emu.grasscutter.utils.Utils.getCurrentSeconds());
                    q.save();
                    finished++;
                }
            }

            // Forge EVERY area Talk gate - locked pillars auto-unlock on EnterTrans; F tip ready.
            finished += this.forgeAllStatueTalkGates();
        } catch (Throwable t) {
            emu.grasscutter.Grasscutter.getLogger()
                    .warn("Statue quest silent-finish failed uid={}", this.player.getUid(), t);
        }
        emu.grasscutter.Grasscutter.getLogger()
                .debug(
                        "Statue talk gates ready uid={} count={} (no quest prerequisite for F)",
                        this.player.getUid(),
                        finished);
    }

    static boolean shouldBypassStarterStatueQuest() {
        return shouldBypassStarterStatueQuest(
                GAME_OPTIONS.questing.enabled,
                emu.grasscutter.config.Configuration.SERVER.game.enableScriptInBigWorld);
    }

    static boolean shouldBypassStarterStatueQuest(boolean questingEnabled, boolean scriptsEnabled) {
        return !questingEnabled || !scriptsEnabled;
    }

    /** Ensure starter statue exists; do not mass-unlock every statue (map fog is per-statue). */
    private void seedUnlockedStatuesFromLegacyUnlockAll() {
        final int sceneId = 3;
        var unlocked = this.player.getUnlockedScenePoints(sceneId);
        // Always keep the Starfell statue available as a starter unlock.
        unlocked.add(7);
        // Clear force-lock on starter if any.
        this.player.getForceLockedScenePoints(sceneId).remove(7);
    }

    /**
     * [v32] 把头像表里“需要任务/道具/角色才能解锁”的条目，在客户端侧全部伪造为已满足，
     * 让头像选择界面不再画锁：
     *  - BY_PARENT_QUEST -> forgeQuest(questId, 0, 3=FINISHED)
     *  - BY_ITEM        -> 直接发放对应道具
     *  - BY_AVATAR      -> 跳过
     */
    public void forgeProfilePictureUnlocks() {
        try {
            java.util.List<Integer> quests = new java.util.ArrayList<>();
            java.util.Set<Integer> seen = new java.util.HashSet<>();
            int items = 0, avatars = 0;
            for (BeyondProfilePictureTable.Entry e : BeyondProfilePictureTable.all()) {
                if (e.unlockParam <= 0) continue;
                String t = e.unlockType == null ? "" : e.unlockType;
                if (t.contains("BY_PARENT_QUEST")) {
                    if (seen.add(e.unlockParam)) {
                        quests.add(e.unlockParam);
                    }
                } else if (t.contains("BY_ITEM")) {
                    try {
                        boolean ok = this.player.getInventory().addItem(e.unlockParam, 1);
                        if (ok) {
                            items++;
                            var gi = this.player.getInventory().getItemById(e.unlockParam);
                            if (gi != null) {
                                var used =
                                        this.player
                                                .getServer()
                                                .getInventorySystem()
                                                .useItem(this.player, 0, gi.getGuid(), 1, 0, false);
                                if (used != null) {
                                    this.player.sendPacket(
                                            new emu.grasscutter.server.packet.send.PacketUseItemRsp(
                                                    0, used));
                                    this.player.getInventory().addItem(e.unlockParam, 1);
                                }
                            }
                        }
                    } catch (Throwable ex) {
                        emu.grasscutter.Grasscutter.getLogger()
                                .warn("MIAO BY_ITEM fail item={} {}", e.unlockParam, ex.toString());
                    }
                }
            }
            if (!quests.isEmpty()) {
                int[] ids = new int[quests.size()];
                for (int i = 0; i < ids.length; i++) ids[i] = quests.get(i);
                this.player.sendPacket(new emu.grasscutter.server.packet.send.PacketFinishedParentQuestUpdateNotify(ids));
            }
            try {
                this.player.sendPacket(new emu.grasscutter.server.packet.send.PacketProfilePictureUnlockNotify(this.player));
            } catch (Throwable t) {
                emu.grasscutter.Grasscutter.getLogger().warn("MIAO unlockNotify fail: {}", t.toString());
            }
            emu.grasscutter.Grasscutter.getLogger()
                    .info("MIAO forgeProfilePicture parentQuests={} items={} avatars={}", quests.size(), items, avatars);
        } catch (Throwable t) {
            emu.grasscutter.Grasscutter.getLogger().warn("MIAO forgeProfilePicture fail: {}", t.toString());
        }
    }

    /** Forge all SotS TalkExcel gate quests as FINISHED for the client. */
    private int forgeAllStatueTalkGates() {
        var forged = this.buildForgedStatueTalkQuests();
        if (forged.isEmpty()) return 0;
        this.player.sendPacket(new PacketQuestListUpdateNotify(forged));
        return forged.size();
    }

    /**
     * Client-only FINISHED quest entries for every known statue Talk gate (303xx), so goddess F
     * needs no quest playthrough. Used by login QuestListNotify and EnterTrans refresh.
     */
    public java.util.List<emu.grasscutter.net.proto.QuestOuterClass.Quest>
            buildForgedStatueTalkQuests() {
        var out = new java.util.ArrayList<emu.grasscutter.net.proto.QuestOuterClass.Quest>();
        var seen = new java.util.HashSet<Integer>();

        for (var e :
                emu.grasscutter.game.managers.StatueTalkQuests.all().int2IntEntrySet()) {
            int questId = e.getIntValue();
            if (questId <= 0 || !seen.add(questId)) continue;

            var existing = this.player.getQuestManager().getQuestById(questId);
            if (existing != null) {
                if (existing.getState() != QuestState.QUEST_STATE_FINISHED) {
                    existing.setState(QuestState.QUEST_STATE_FINISHED);
                    existing.setFinishTime(emu.grasscutter.utils.Utils.getCurrentSeconds());
                    existing.save();
                }
                continue;
            }
            out.add(
                    emu.grasscutter.server.packet.send.PacketQuestListUpdateNotify.forgeQuest(
                            questId, 303, 3));
            this.player.getForgedStatueTalkQuests().add(questId);
        }
        return out;
    }

    /** Re-push Talk gate for one unlocked statue (EnterTrans / after unlock). */
    public void refreshStatueTalkGate(int sceneId, int pointId) {
        var entry = GameData.getScenePointEntryById(sceneId, pointId);
        if (entry == null || entry.getPointData() == null) return;
        if (!emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(entry.getPointData())) {
            return;
        }
        if (!this.player.getUnlockedScenePoints(sceneId).contains(pointId)) return;
        if (this.player.isScenePointForceLocked(sceneId, pointId)) return;

        int areaId = entry.getPointData().getAreaId();
        int questId = emu.grasscutter.game.managers.StatueTalkQuests.questForArea(areaId);
        if (questId > 0) {
            var existing = this.player.getQuestManager().getQuestById(questId);
            if (existing != null) {
                // Only notify when state actually flips - re-sending FINISHED on every EnterTrans
                // makes the client replay quest rewards (character EXP x20 spam).
                if (existing.getState() != QuestState.QUEST_STATE_FINISHED) {
                    existing.setState(QuestState.QUEST_STATE_FINISHED);
                    existing.setFinishTime(emu.grasscutter.utils.Utils.getCurrentSeconds());
                    existing.save();
                    this.player.sendPacket(new PacketQuestListUpdateNotify(existing));
                }
            } else if (this.player.getForgedStatueTalkQuests().add(questId)) {
                // No QuestExcel: forge once per session (login QuestListNotify also injects these).
                this.player.sendPacket(new PacketQuestListUpdateNotify(questId, 303, 3));
            }
        }
        if (areaId > 0) {
            // Client Talk also needs QUEST_COND_SCENE_AREA_UNLOCKED for this area.
            this.unlockSceneAreaHierarchy(sceneId, areaId);
        }
        // Goddess NPC: one GroupSuiteNotify per visit (debounced) - keeps F without SFX thrash.
        this.refreshStatueGoddessNpc(sceneId, pointId);
    }

    /**
     * Load goddess NPC suite once per visit (F tip). Always unload on exit - leaving suites
     * loaded is what made Fontaine ambient/SFX intermittent after visiting a statue.
     *
     * <p>Nod-Krai City 7 (1515-1517) has no SceneNpcBorn for 790x - spawn an invisible goddess
     * NPC entity at the statue as fallback.
     */
    public void refreshStatueGoddessNpc(int sceneId, int pointId) {
        var spe = GameData.getScenePointEntryById(sceneId, pointId);
        if (spe == null || spe.getPointData() == null) return;
        var pd = spe.getPointData();
        if (!emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(pd)) return;

        int npcId = pd.getNpcId();
        if (npcId <= 0) {
            npcId = emu.grasscutter.game.managers.StatueTalkQuests.npcForArea(pd.getAreaId());
        }
        var statuePos = pd.getPos() != null ? pd.getPos() : pd.getTranPos();

        var bornData = GameData.getSceneNpcBornData().get(sceneId);
        if (bornData != null && bornData.getBornPosList() != null) {
            for (var born : bornData.getBornPosList()) {
                boolean match =
                        (npcId > 0 && born.getConfigId() == npcId)
                                || (statuePos != null
                                        && born.getPos() != null
                                        && statuePos.computeDistance(born.getPos()) < 5f
                                        && emu.grasscutter.game.managers.StatueTalkQuests
                                                .isGoddessNpc(born.getConfigId()));
                if (!match) continue;
                if (born.getGroupId() <= 0) continue;

                var notified = this.player.getGoddessSuiteNotified();
                if (!notified.add(born.getGroupId())) {
                    return; // already loaded this visit
                }

                int suite =
                        (born.getSuiteIdList() != null && !born.getSuiteIdList().isEmpty())
                                ? born.getSuiteIdList().get(0)
                                : 1;
                this.player.sendPacket(
                        new emu.grasscutter.server.packet.send.PacketGroupSuiteNotify(
                                born.getGroupId(), suite));
                emu.grasscutter.Grasscutter.getLogger()
                        .debug(
                                "Force goddess suite uid={} point={} group={} npc={} suite={}",
                                this.player.getUid(),
                                pointId,
                                born.getGroupId(),
                                born.getConfigId(),
                                suite);
                // Natlan Pyro is quest-gated in the CLIENT talk tree ("cannot resonate with Pyro").
                // Spawn a Talk-style Worktop resonate that skips the gate without force-finishing
                // the Archon quest - players keep the related quest receivable, no GM command needed.
                this.maybeSpawnQuestBypassResonateProxy(sceneId, pointId, pd, statuePos);
                return;
            }
        }

        this.spawnStatueGoddessNpc(sceneId, pointId, pd, npcId, statuePos);
    }

    /**
     * For nations whose statue Talk blocks element resonate behind a quest (Natlan Pyro), spawn a
     * small Worktop ring with option {@code 1159} "resonate with the statue" so normal players can switch without
     * {@code /se} and without {@code forcefinish}.
     */
    private void maybeSpawnQuestBypassResonateProxy(
            int sceneId,
            int pointId,
            emu.grasscutter.data.common.PointData pd,
            emu.grasscutter.game.world.Position statuePos) {
        int areaId = pd.getAreaId();
        var sots = this.player.getSotsManager();
        if (sots == null || areaId <= 0) return;
        var city = sots.getCityByAreaId(areaId);
        // City 6 = Natlan (Fire). Extend here if another nation gets a similar client gate.
        if (city == null || city.getCityId() != 6) return;
        if (this.player.getGoddessNpcEntityByPoint().containsKey(pointId)) return;

        var scene = this.player.getScene();
        if (scene == null || scene.getId() != sceneId) return;

        var rot =
                pd.getTranRot() != null
                        ? pd.getTranRot()
                        : (pd.getRot() != null
                                ? pd.getRot()
                                : new emu.grasscutter.game.world.Position());

        var spawnPoints = new java.util.ArrayList<emu.grasscutter.game.world.Position>();
        // 代理只放在锚点（雕像）本体位置，不再跟玩家、不再锦环
        {
            var c0 = pd.getPos() != null ? pd.getPos() : pd.getTranPos();
            if (c0 != null) spawnPoints.add(c0.clone());
        }
        var unique = new java.util.ArrayList<emu.grasscutter.game.world.Position>();
        for (var p : spawnPoints) {
            boolean near = false;
            for (var u : unique) {
                if (u.computeDistance(p) < 2f) {
                    near = true;
                    break;
                }
            }
            if (!near) unique.add(p);
        }
        if (unique.isEmpty()) return;

        int synthGroup = syntheticGoddessGroupId(pointId);
        this.player.getGoddessSuiteNotified().add(synthGroup);

        var entityIds = new java.util.ArrayList<Integer>();
        for (int i = 0; i < unique.size(); i++) {
            var gadget =
                    new emu.grasscutter.game.entity.EntityGadget(
                            scene, 70360001, unique.get(i), rot);
            gadget.setConfigId(pointId * 100 + 50 + i);
            gadget.setGroupId(synthGroup);
            gadget.setInteractEnabled(true);
            gadget.buildContent();
            if (gadget.getContent()
                    instanceof emu.grasscutter.game.entity.gadget.GadgetWorktop worktop) {
                worktop.addWorktopOptions(new int[] {1159}); // resonate with the statue
                worktop.setOnSelectWorktopOptionEvent(
                        (ctx, option) -> {
                            try {
                                this.handleStatueResonate(areaId);
                            } catch (Throwable t) {
                                emu.grasscutter.Grasscutter.getLogger()
                                        .warn(
                                                "Natlan resonate proxy failed uid={} point={}",
                                                this.player.getUid(),
                                                pointId,
                                                t);
                            }
                            return false;
                        });
            }
            scene.addEntity(gadget);
            scene.broadcastPacket(
                    new emu.grasscutter.server.packet.send.PacketWorktopOptionNotify(gadget));
            entityIds.add(gadget.getId());
        }
        this.player.getGoddessNpcEntityByPoint().put(pointId, entityIds);
        emu.grasscutter.Grasscutter.getLogger()
                .debug(
                        "Spawn Natlan pyro-resonate proxies uid={} point={} count={} area={}",
                        this.player.getUid(),
                        pointId,
                        entityIds.size(),
                        areaId);
    }

    /** Synthetic group id for spawned goddess NPCs (avoids colliding with real 133xxxxxx groups). */
    private static int syntheticGoddessGroupId(int pointId) {
        return 910_000_000 + pointId;
    }

    private void spawnStatueGoddessNpc(
            int sceneId,
            int pointId,
            emu.grasscutter.data.common.PointData pd,
            int npcId,
            emu.grasscutter.game.world.Position statuePos) {
        if (npcId <= 0) {
            npcId = emu.grasscutter.game.managers.StatueTalkQuests.npcForArea(pd.getAreaId());
        }

        var scene = this.player.getScene();
        if (scene == null || scene.getId() != sceneId) return;

        int synthGroup = syntheticGoddessGroupId(pointId);
        if (!this.player.getGoddessSuiteNotified().add(synthGroup)) {
            return;
        }
        if (this.player.getGoddessNpcEntityByPoint().containsKey(pointId)) {
            return;
        }

        var rot =
                pd.getTranRot() != null
                        ? pd.getTranRot()
                        : (pd.getRot() != null
                                ? pd.getRot()
                                : new emu.grasscutter.game.world.Position());

        // Thick Nod-Krai pillars block standing on center - ring of proxies + player/tranPos.
        var spawnPoints = new java.util.ArrayList<emu.grasscutter.game.world.Position>();
        // 代理只放在锚点（柱子）本体位置，不再锦 8/12m 大环
        {
            var c0 = pd.getPos() != null ? pd.getPos() : pd.getTranPos();
            if (c0 != null) spawnPoints.add(c0.clone());
        }
        if (spawnPoints.isEmpty()) return;

        // Deduplicate near-identical positions (<2m).
        var unique = new java.util.ArrayList<emu.grasscutter.game.world.Position>();
        for (var p : spawnPoints) {
            boolean near = false;
            for (var u : unique) {
                if (u.computeDistance(p) < 2f) {
                    near = true;
                    break;
                }
            }
            if (!near) unique.add(p);
        }

        int areaId = pd.getAreaId();
        var entityIds = new java.util.ArrayList<Integer>();
        for (int i = 0; i < unique.size(); i++) {
            var spawnPos = unique.get(i);
            var gadget =
                    new emu.grasscutter.game.entity.EntityGadget(scene, 70360001, spawnPos, rot);
            gadget.setConfigId(pointId * 100 + i); // unique config per proxy
            gadget.setGroupId(synthGroup);
            gadget.setInteractEnabled(true);
            gadget.buildContent();
            if (gadget.getContent()
                    instanceof emu.grasscutter.game.entity.gadget.GadgetWorktop worktop) {
                // Client OptionExcel labels (stock client):
                // 10010001 = touch (Talk) - heal + offer
                // 1159 = resonate with the statue (Talk) - Traveler element resonate
                // Do NOT use 1/2 (open door / unlock wind field gear icons).
                worktop.addWorktopOptions(new int[] {10010001, 1159});
                worktop.setOnSelectWorktopOptionEvent(
                        (ctx, option) -> {
                            try {
                                this.handleStatueProxyOption(areaId, sceneId, option);
                            } catch (Throwable t) {
                                emu.grasscutter.Grasscutter.getLogger()
                                        .warn(
                                                "Statue proxy option failed uid={} point={} opt={}",
                                                this.player.getUid(),
                                                pointId,
                                                option,
                                                t);
                            }
                            return false;
                        });
            }
            scene.addEntity(gadget);
            scene.broadcastPacket(
                    new emu.grasscutter.server.packet.send.PacketWorktopOptionNotify(gadget));
            entityIds.add(gadget.getId());
        }

        this.player.getGoddessNpcEntityByPoint().put(pointId, entityIds);
        emu.grasscutter.Grasscutter.getLogger()
                .debug(
                        "Spawn statue worktop proxies uid={} point={} npc={} count={} area={}",
                        this.player.getUid(),
                        pointId,
                        npcId,
                        entityIds.size(),
                        areaId);
    }

    /**
     * Statue Worktop proxy (Nod-Krai / Snezhnaya). Uses client OptionExcel ids so the F-menu
     * shows Talk-style labels instead of the gear "open door / unlock wind field" ones:
     * <ul>
     *   <li>{@code 10010001} touch - heal + offer (plus auto-resonate if the Traveler is on-field)
     *   <li>{@code 1159} resonate with the statue - Traveler element resonate only
     * </ul>
     */
    private void handleStatueProxyOption(int areaId, int sceneId, int option) {
        // 1159 = resonate only; also accept legacy option 2 for old sessions.
        if (option == 1159 || option == 2) {
            this.handleStatueResonate(areaId);
            return;
        }

        var sots = this.player.getSotsManager();
        if (sots == null) return;

        int beforeHpSum = 0;
        for (var avatar : this.player.getTeamManager().getActiveTeam()) {
            beforeHpSum +=
                    (int) avatar.getFightProperty(emu.grasscutter.game.props.FightProperty.FIGHT_PROP_CUR_HP);
        }
        sots.refillSpringVolume();
        for (var avatar : this.player.getTeamManager().getActiveTeam()) {
            sots.checkAndHealAvatar(avatar);
        }
        int afterHpSum = 0;
        for (var avatar : this.player.getTeamManager().getActiveTeam()) {
            afterHpSum +=
                    (int) avatar.getFightProperty(emu.grasscutter.game.props.FightProperty.FIGHT_PROP_CUR_HP);
        }

        if (areaId <= 0) {
            this.player.dropMessage("Statue proxy: attempted to heal.");
            this.handleStatueResonate(areaId);
            return;
        }
        var city = sots.getCityByAreaId(areaId);
        if (city == null) {
            this.player.dropMessage(
                    "Statue proxy: healed (area=" + areaId + " has no city binding, cannot offer).");
            this.handleStatueResonate(areaId);
            return;
        }

        int cityId = city.getCityId();
        String cityName =
                switch (cityId) {
                    case 7 -> "Nod-Krai";
                    case 8 -> "Snezhnaya";
                    case 6 -> "Natlan";
                    case 5 -> "Fontaine";
                    default -> ("City " + cityId);
                };
        var cityInfo = sots.getCityInfo(cityId);

        // Resolve cost item (StatuePromote or CityLevelup).
        int costItemId = 0;
        var promote =
                emu.grasscutter.data.GameData.getStatuePromoteData(cityId, cityInfo.getLevel() + 1);
        var cityLevelup =
                emu.grasscutter.data.GameData.getCityLevelupData(cityId, cityInfo.getLevel() + 1);
        if (promote != null
                && promote.getCostItems() != null
                && promote.getCostItems().length > 0
                && promote.getCostItems()[0] != null) {
            costItemId = promote.getCostItems()[0].getId();
        } else if (cityLevelup != null) {
            costItemId = cityLevelup.getCostItemId();
        }

        int have = costItemId > 0 ? this.player.getInventory().getItemCountById(costItemId) : 0;
        if (costItemId <= 0) {
            this.player.dropMessage("Statue proxy: healed (" + cityName + " is max level or has no next level).");
        } else if (have <= 0) {
            this.player.dropMessage(
                    "Statue proxy: healed. "
                            + cityName
                            + " Requires oculus id="
                            + costItemId
                            + " (Snezhnaya=107035, Nod-Krai=107030), which the inventory does not have.");
        } else {
            sots.levelUpSotS(areaId, sceneId, have);
            cityInfo = sots.getCityInfo(cityId);
            this.player.dropMessage(
                    String.format(
                            "Statue proxy: %s area=%d heal%s; oculi %d x%d -> city%d Lv.%d (crystals %d)",
                            cityName,
                            areaId,
                            afterHpSum > beforeHpSum ? "ok" : "(already full)",
                            costItemId,
                            have,
                            cityId,
                            cityInfo.getLevel(),
                            cityInfo.getNumCrystal()));
        }

        // After heal/offer, also resonate if Traveler is on-field (official goddess flow).
        this.handleStatueResonate(areaId);
    }

    /** Change on-field Traveler to this statue region's element. */
    private void handleStatueResonate(int areaId) {
        var element = this.resolveStatueElement(areaId);
        if (element == null || element.getDepotIndex() <= 0) {
            this.player.dropMessage("Statue resonance: this area has no resonatable element configured.");
            return;
        }

        var entity = this.player.getTeamManager().getCurrentAvatarEntity();
        if (entity == null || entity.getAvatar() == null) {
            this.player.dropMessage("Statue resonance: no active character.");
            return;
        }
        var avatar = entity.getAvatar();
        int avatarId = avatar.getAvatarId();
        if (avatarId != emu.grasscutter.GameConstants.MAIN_CHARACTER_MALE
                && avatarId != emu.grasscutter.GameConstants.MAIN_CHARACTER_FEMALE) {
            this.player.dropMessage(
                    "Statue resonance: make the Traveler the active character first, then pick resonate.");
            return;
        }

        String elemName =
                switch (element) {
                    case Wind -> "Anemo";
                    case Rock -> "Geo";
                    case Electric -> "Electro";
                    case Grass -> "Dendro";
                    case Water -> "Hydro";
                    case Fire -> "Pyro";
                    case Ice -> "Cryo";
                    default -> element.name();
                };

        if (!avatar.changeElement(element, true)) {
            // Already that element, or depot missing.
            if (this.player.getMainCharacterElement() == element
                    || (avatar.getSkillDepot() != null
                            && avatar.getSkillDepot().getElementType() == element)) {
                this.player.dropMessage("Statue resonance: the Traveler is already " + elemName + ".");
            } else {
                this.player.dropMessage("Statue resonance: cannot switch to " + elemName + " (missing skill depot?).");
            }
            return;
        }

        avatar.save();
        this.player.save();
        // No quest force-finish - Archon quests stay receivable.
        this.player.dropMessage(
                "Statue resonance: the Traveler switched to " + elemName + " (no quest progress consumed).");
    }

    /**
     * Prefer WorldAreaConfig element for the statue area; fall back to city defaults when the
     * area row is missing (e.g. some Snezhnaya sub-areas).
     */
    private emu.grasscutter.game.props.ElementType resolveStatueElement(int areaId) {
        if (areaId > 0) {
            var area = emu.grasscutter.data.GameData.getWorldAreaDataMap().get(areaId);
            if (area != null
                    && area.getElementType() != null
                    && area.getElementType().getDepotIndex() > 0) {
                return area.getElementType();
            }
        }
        var sots = this.player.getSotsManager();
        if (sots == null || areaId <= 0) return null;
        var city = sots.getCityByAreaId(areaId);
        if (city == null) return null;
        return switch (city.getCityId()) {
            case 1 -> emu.grasscutter.game.props.ElementType.Wind;
            case 2 -> emu.grasscutter.game.props.ElementType.Rock;
            case 3 -> emu.grasscutter.game.props.ElementType.Electric;
            case 4 -> emu.grasscutter.game.props.ElementType.Grass;
            case 5 -> emu.grasscutter.game.props.ElementType.Water;
            case 6 -> emu.grasscutter.game.props.ElementType.Fire;
            case 7, 8 -> emu.grasscutter.game.props.ElementType.Ice; // Nod-Krai / Snezhnaya, per the current table
            default -> null;
        };
    }

    /** Unload goddess client suite on leave so ambient/SFX are not left half-broken. */
    public void clearStatueGoddessNotify(int sceneId, int pointId) {
        var spe = GameData.getScenePointEntryById(sceneId, pointId);
        if (spe == null || spe.getPointData() == null) return;
        if (!emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(spe.getPointData())) {
            return;
        }

        int npcId = spe.getPointData().getNpcId();
        if (npcId <= 0) {
            npcId = emu.grasscutter.game.managers.StatueTalkQuests.npcForArea(
                    spe.getPointData().getAreaId());
        }
        var statuePos =
                spe.getPointData().getPos() != null
                        ? spe.getPointData().getPos()
                        : spe.getPointData().getTranPos();
        var bornData = GameData.getSceneNpcBornData().get(sceneId);
        if (bornData != null && bornData.getBornPosList() != null) {
            for (var born : bornData.getBornPosList()) {
                boolean match =
                        (npcId > 0 && born.getConfigId() == npcId)
                                || (statuePos != null
                                        && born.getPos() != null
                                        && statuePos.computeDistance(born.getPos()) < 5f
                                        && emu.grasscutter.game.managers.StatueTalkQuests
                                                .isGoddessNpc(born.getConfigId()));
                if (!match) continue;
                if (!this.player.getGoddessSuiteNotified().remove(born.getGroupId())) {
                    // Suite already cleared this visit - still drop resonate proxies if any.
                    this.removeStatueWorktopProxies(pointId);
                    return;
                }
                this.player.sendPacket(
                        new emu.grasscutter.server.packet.send.PacketGroupUnloadNotify(
                                java.util.List.of(born.getGroupId())));
                emu.grasscutter.Grasscutter.getLogger()
                        .debug(
                                "Unload goddess suite uid={} point={} group={}",
                                this.player.getUid(),
                                pointId,
                                born.getGroupId());
                this.removeStatueWorktopProxies(pointId);
                return;
            }
        }

        // Spawned-entity fallback (Nod-Krai / thick-pillar proxy ring / Natlan resonate).
        this.removeStatueWorktopProxies(pointId);
    }

    private void removeStatueWorktopProxies(int pointId) {
        int synthGroup = syntheticGoddessGroupId(pointId);
        this.player.getGoddessSuiteNotified().remove(synthGroup);
        var entityIds = this.player.getGoddessNpcEntityByPoint().remove(pointId);
        if (entityIds == null || entityIds.isEmpty()) return;
        var scene = this.player.getScene();
        if (scene == null) return;
        for (int entityId : entityIds) {
            var entity = scene.getEntityById(entityId);
            if (entity != null) {
                scene.removeEntity(entity);
            }
        }
        emu.grasscutter.Grasscutter.getLogger()
                .debug(
                        "Remove statue worktop proxies uid={} point={} count={}",
                        this.player.getUid(),
                        pointId,
                        entityIds.size());
    }

    /**
     * 真解锁神像：标记解锁 + 通知客户端 + **补回神像雕像 gadget** + 铺回血代理。
     *
     * <p>客户端收到解锁通知后会把「未解锁雕像」移除，等服务器给「已激活」实体；AstaPS 原先
     * 不给，所以神像会“灭掉”且不能传送。这里用场景点数据里的 gadgetId 把雕像补回去。
     */
    public void miaoUnlockStatue(int sceneId, int pointId) {
        var entry = GameData.getScenePointEntryById(sceneId, pointId);
        if (entry == null || entry.getPointData() == null) return;
        var pd = entry.getPointData();
        var transition =
                applyTransPointUnlock(
                        this.player.getUnlockedScenePoints(sceneId),
                        this.player.getForceLockedScenePoints(sceneId),
                        pointId);
        boolean isNew = transition.newlyUnlocked();
        if (transition.changed()) {
            this.player.sendPacket(
                    new emu.grasscutter.server.packet.send.PacketScenePointUnlockNotify(
                            sceneId, pointId));
            // 关键：重发场景点列表，客户端的“已解锁/可传送”状态来自这个包。
            this.player.sendPacket(
                    new emu.grasscutter.server.packet.send.PacketGetScenePointRsp(
                            this.player, sceneId));
        }
        try {
            var scene = this.player.getScene();
            if (scene != null && scene.getId() == sceneId && pd.getGadgetId() > 0) {
                int gid = pd.getGadgetId();
                // 已经补过就不再重复补
                boolean exists = false;
                for (var ent : scene.getEntities().values()) {
                    if (ent instanceof emu.grasscutter.game.entity.EntityGadget eg
                            && eg.getGadgetData() != null
                            && eg.getGadgetData().getId() == gid) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    var pos = pd.getPos() != null ? pd.getPos() : pd.getTranPos();
                    var rot = pd.getRot() != null ? pd.getRot() : pd.getTranRot();
                    if (pos != null) {
                        var eg = new emu.grasscutter.game.entity.EntityGadget(
                                scene, gid, pos, rot != null ? rot : new emu.grasscutter.game.world.Position());
                        scene.addEntity(eg);
                    }
                }
            }
        } catch (Throwable t) {
        }
        this.refreshStatueGoddessNpc(sceneId, pointId);
    }

    /**
     * Lock a quest-controlled scene point on the client and in persisted player state.
     * ScenePointUnlockNotify's generated field 4 is the 7.1 locked-point wire field.
     */
    public boolean lockTransPoint(int sceneId, int pointId) {
        if (sceneId <= 0 || pointId <= 0) return false;
        applyQuestPointLock(this.player.getForceLockedScenePoints(sceneId), pointId);
        this.player.sendPacket(PacketScenePointUnlockNotify.lock(sceneId, pointId));
        this.player.save();
        return true;
    }

    public boolean unlockTransPoint(int sceneId, int pointId, boolean isStatue) {
        ScenePointEntry scenePointEntry = GameData.getScenePointEntryById(sceneId, pointId);
        if (scenePointEntry == null) {
            return false;
        }

        var transition =
                applyTransPointUnlock(
                        this.player.getUnlockedScenePoints(sceneId),
                        this.player.getForceLockedScenePoints(sceneId),
                        pointId);
        if (!transition.changed()) {
            return false;
        }
        boolean newlyUnlocked = transition.newlyUnlocked();

        var pointData = scenePointEntry.getPointData();
        if (!isStatue
                && emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(pointData)) {
            isStatue = true;
        }

        // Permanent exploration state survives QUEST_EXEC_LOCK_POINT. Only a genuine first unlock
        // opens map areas and grants exploration rewards; releasing a quest lock still counts as an
        // unlock event for quest/script logic below.
        if (newlyUnlocked && isStatue && pointData != null && pointData.getAreaId() > 0) {
            this.unlockSceneAreaHierarchy(sceneId, pointData.getAreaId());
        }

        // Unlock rewards: statues vs waypoints/dungeon entries. Never duplicate them when a quest
        // temporarily re-locks a point the player had already discovered.
        if (newlyUnlocked && isStatue) {
            this.player.getInventory().addItem(201, 2888, ActionReason.UnlockPointReward); // primogems
            this.player.getInventory().addItem(102, 800, ActionReason.UnlockPointReward); // adventure EXP
            this.player.getInventory().addItem(107009, 3, ActionReason.UnlockPointReward); // fragile resin
            this.player.getInventory().addItem(104003, 50, ActionReason.UnlockPointReward); // hero's wit
            this.player.getInventory().addItem(104013, 20, ActionReason.UnlockPointReward); // mystic enhancement ore
        } else if (newlyUnlocked) {
            // Ordinary waypoints use their scene-point reward, not a fixed grant.
            var pointReward =
                    GameData.getTransPointRewardDataMap().get((sceneId << 16) + pointId);
            if (pointReward != null) {
                var reward = GameData.getRewardDataMap().get(pointReward.getRewardId());
                if (reward != null) {
                    this.player.getInventory()
                            .addItemParamDatas(reward.getAllRewardItems(), ActionReason.UnlockPointReward);
                } else {
                    emu.grasscutter.Grasscutter.getLogger()
                            .warn("Missing reward {} for scene {} point {}",
                                    pointReward.getRewardId(), sceneId, pointId);
                }
            }
        }

        // Fire quest trigger for trans point unlock.
        this.player
                .getQuestManager()
                .queueEvent(QuestContent.QUEST_CONTENT_UNLOCK_TRANS_POINT, sceneId, pointId);
        try {
            if (this.player.getScene() != null) {
                this.player
                        .getScene()
                        .getScriptManager()
                        .callEvent(new ScriptArgs(0, EVENT_UNLOCK_TRANS_POINT, sceneId, pointId));
            }
        } catch (Throwable ignored) {
        }

        // After unlock, forge Talk gate finished so goddess F works without playing 303xx.
        if (isStatue) {
            this.refreshStatueTalkGate(sceneId, pointId);
        }

        // Send packet.
        this.player.sendPacket(new PacketScenePointUnlockNotify(sceneId, pointId));
        try {
            int total = 0;
            if (this.player.getUnlockedScenePoints() != null) {
                for (var pts : this.player.getUnlockedScenePoints().values()) {
                    if (pts != null) total += pts.size();
                }
            }
            InvestigationHandbookHelper.trigger(
                    this.player,
                    emu.grasscutter.game.props.WatcherTriggerType.TRIGGER_UNLOCK_TRANS_POINT,
                    0,
                    total);
        } catch (Throwable ignored) {
        }
        this.player.save();
        return true;
    }

    public void unlockSceneArea(int sceneId, int areaId) {
        // Add the area to the list of unlocked areas in its scene.
        this.player.getUnlockedSceneAreas(sceneId).add(areaId);

        // Send packet.
        this.player.sendPacket(new PacketSceneAreaUnlockNotify(sceneId, areaId));
        try {
            InvestigationHandbookHelper.trigger(
                    this.player,
                    emu.grasscutter.game.props.WatcherTriggerType.TRIGGER_UNLOCK_AREA,
                    areaId,
                    1);
        } catch (Throwable ignored) {
        }
    }

    /**
     * Unlock a world area and its hierarchy so statue fog matches the official "one statue gives one
     * region" behaviour (LEVEL_1 parent + LEVEL_2 children).
     */
    public void unlockSceneAreaHierarchy(int sceneId, int areaId) {
        if (areaId <= 0) return;
        var newly = new java.util.LinkedHashSet<Integer>();
        var areas = this.player.getUnlockedSceneAreas(sceneId);
        if (areas.add(areaId)) newly.add(areaId);

        for (var worldArea : GameData.getWorldAreaDataMap().values()) {
            if (worldArea.getSceneId() != sceneId) continue;
            // Children of this LEVEL_1 area.
            if (worldArea.getParentArea() == areaId && worldArea.getChildArea() > 0) {
                if (areas.add(worldArea.getChildArea())) newly.add(worldArea.getChildArea());
            }
            // If this id is a child, also unlock its parent.
            if (worldArea.getChildArea() == areaId && worldArea.getParentArea() > 0) {
                if (areas.add(worldArea.getParentArea())) newly.add(worldArea.getParentArea());
            }
        }

        if (!newly.isEmpty()) {
            this.player.sendPacket(new PacketSceneAreaUnlockNotify(sceneId, newly));
            this.player.save();
            try {
                for (int unlockedAreaId : newly) {
                    InvestigationHandbookHelper.trigger(
                            this.player,
                            emu.grasscutter.game.props.WatcherTriggerType.TRIGGER_UNLOCK_AREA,
                            unlockedAreaId,
                            1);
                }
            } catch (Throwable ignored) {
            }
        }
    }

    /**
     * Rebuild scene areas from unlocked statues only so map fog tracks SotS unlocks, not waypoints.
     */
    public void syncSceneAreasFromUnlockedPoints(int sceneId) {
        var pointIds = GameData.getScenePointsPerScene().get(sceneId);
        if (pointIds == null) return;

        var justified = new java.util.LinkedHashSet<Integer>();
        if (sceneId == 3) {
            justified.add(1); // starter Mondstadt
        }
        var unlockedPoints = this.player.getUnlockedScenePoints(sceneId);
        for (int pointId : pointIds) {
            if (!unlockedPoints.contains(pointId)) continue;
            if (this.player.isScenePointForceLocked(sceneId, pointId)) continue;
            var entry = GameData.getScenePointEntryById(sceneId, pointId);
            if (entry == null || entry.getPointData() == null) continue;
            // Waypoints share areaId with statues - only SotS should justify fog.
            if (!emu.grasscutter.game.managers.StatueTalkQuests.isStatuePoint(
                    entry.getPointData())) {
                continue;
            }
            int areaId = entry.getPointData().getAreaId();
            if (areaId <= 0) continue;
            justified.add(areaId);
            for (var worldArea : GameData.getWorldAreaDataMap().values()) {
                if (worldArea.getSceneId() != sceneId) continue;
                if (worldArea.getParentArea() == areaId && worldArea.getChildArea() > 0) {
                    justified.add(worldArea.getChildArea());
                }
                if (worldArea.getChildArea() == areaId && worldArea.getParentArea() > 0) {
                    justified.add(worldArea.getParentArea());
                }
            }
        }

        var areas = this.player.getUnlockedSceneAreas(sceneId);
        areas.clear();
        areas.addAll(justified);
        this.player.save();
        this.player.sendPacket(new PacketSceneAreaUnlockNotify(sceneId, justified));
        emu.grasscutter.Grasscutter.getLogger()
                .debug(
                        "Synced scene areas from points uid={} scene={} areas={}",
                        this.player.getUid(),
                        sceneId,
                        justified.size());
    }

    /** Give replace costume to player (Amber, Jean, Mona, Rosaria) */
    public void addReplaceCostumes() {
        var currentPlayerCostumes = player.getCostumeList();
        GameData.getAvatarReplaceCostumeDataMap()
                .keySet()
                .forEach(
                        costumeId -> {
                            if (GameData.getAvatarCostumeDataMap().get(costumeId) == null
                                    || currentPlayerCostumes.contains(costumeId)) {
                                return;
                            }
                            this.player.addCostume(costumeId);
                        });
    }

    /** Quest progress */
    public void addQuestProgress(int id, int count) {
        var newCount = player.getPlayerProgress().addToCurrentProgress(String.valueOf(id), count);
        player.save();
        player
                .getQuestManager()
                .queueEvent(QuestContent.QUEST_CONTENT_ADD_QUEST_PROGRESS, id, newCount);
    }

    /** Item history */
    public void addItemObtainedHistory(int id, int count) {
        var newCount = player.getPlayerProgress().addToItemHistory(id, count);
        player.save();
        player.getQuestManager().queueEvent(QuestCond.QUEST_COND_HISTORY_GOT_ANY_ITEM, id, newCount);
    }

    /******************************************************************************************************************
     ******************************************************************************************************************
     * SCENETAGS
     ******************************************************************************************************************
     *****************************************************************************************************************/
    public void addSceneTag(int sceneId, int sceneTagId) {
        player.getSceneTags().computeIfAbsent(sceneId, k -> new HashSet<>()).add(sceneTagId);
        player.sendPacket(new PacketPlayerWorldSceneInfoListNotify(player));
    }

    public void delSceneTag(int sceneId, int sceneTagId) {
        // Sanity check
        if (player.getSceneTags().get(sceneId) == null) {
            // Can't delete something that doesn't exist
            return;
        }
        player.getSceneTags().get(sceneId).remove(sceneTagId);
        player.sendPacket(new PacketPlayerWorldSceneInfoListNotify(player));
    }

    public boolean checkSceneTag(int sceneId, int sceneTagId) {
        return player.getSceneTags().get(sceneId).contains(sceneTagId);
    }
}
