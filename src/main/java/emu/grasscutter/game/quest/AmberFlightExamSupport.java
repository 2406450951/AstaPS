package emu.grasscutter.game.quest;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.game.dungeons.challenge.WorldChallenge;
import emu.grasscutter.game.dungeons.challenge.trigger.InTimeTrigger;
import emu.grasscutter.game.dungeons.challenge.trigger.TriggerGroupTriggerTrigger;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.quest.enums.ParentQuestState;
import emu.grasscutter.game.quest.enums.QuestState;
import emu.grasscutter.game.world.Scene;
import emu.grasscutter.scripts.data.SceneGroup;
import emu.grasscutter.scripts.data.SceneTrigger;
import emu.grasscutter.server.packet.send.PacketChallengeDataNotify;
import emu.grasscutter.server.packet.send.PacketDungeonChallengeFinishNotify;
import java.util.List;

/** Scene adapter for the 60-second, six-marker flight exam; never completes it at login. */
public final class AmberFlightExamSupport {
    private static final int GROUP = 133001244;
    private AmberFlightExamSupport() {}

    static boolean isActive(GameQuest quest) {
        if (!QuestManager.isQuestingActive() || quest == null
                || quest.getMainQuestId() != 480 || quest.getSubQuestId() != 48002
                || quest.getState() != QuestState.QUEST_STATE_UNFINISHED) return false;
        var main = quest.getMainQuest();
        if (main == null || main.isFinished()
                || main.getState() == ParentQuestState.PARENT_QUEST_STATE_FINISHED) return false;
        var ready = main.getChildQuestById(48001);
        return ready != null && ready.getState() == QuestState.QUEST_STATE_FINISHED;
    }

    static boolean preserveOnLogin(GameMainQuest main) {
        return main != null && main.getParentQuestId() == 480
                && isActive(main.getChildQuestById(48002));
    }

    public static void ensure(GameQuest quest) {
        if (!isActive(quest)) return;
        var player = quest.getOwner();
        if (player.getSceneLoadState() != Player.SceneLoadState.LOADED) return;
        var world = player.getWorld();
        if (world == null) return;
        var scene = world.getSceneById(3);
        if (scene == null) return;
        var scripts = scene.getScriptManager();
        scripts.whenInitialized(ready -> {
            if (!ready) return;
            scene.runWhenPlayerEnters(player, () -> scene.runWhenFinished(() -> scene.getScheduler().scheduleDelayedTask(() -> {
                if (!isActive(quest) || player.getWorld() != world || player.getSceneId() != 3
                        || world.getScenes().get(3) != scene || scripts.isDestroyed()) return;
                var fresh = scripts.getGroupInstanceById(GROUP) == null;
                var group = scripts.getGroupById(GROUP);
                if (group == null || group.getScript() == null) {
                    Grasscutter.getLogger().warn("Amber flight exam group {} is unavailable", GROUP);
                    return;
                }
                group.setDontUnload(true);
                var running = scene.getChallenge();
                boolean activeAttempt = running != null && running.inProgress()
                        && isFlightChallenge(scene, running.getGroup(), running.getChallengeIndex(), running.getChallengeId());
                if (fresh || (!activeAttempt && scripts.getVariables(GROUP).getOrDefault("started", 0) != 0)) {
                    // A retry/relogin gets a fresh timed attempt, not an old cached started flag.
                    for (int suite = 2; suite <= 8; suite++) {
                        var data = group.getSuiteByIndex(suite);
                        if (data != null) scripts.removeGroupSuite(group, data);
                    }
                    scripts.refreshGroupSuite(GROUP, 1);
                    scripts.getVariables(GROUP).put("started", 0);
                    scripts.getVariables(GROUP).put("step", 0);
                }
                var main = quest.getMainQuest();
                if (main.getQuestGroupSuites().stream().noneMatch(gs -> gs.getScene() == 3 && gs.getGroup() == GROUP)) {
                    main.getQuestGroupSuites().add(QuestGroupSuite.of().scene(3).group(GROUP).suite(1).build());
                    main.save();
                }
            }, 1)));
        });
    }

    public static void onSceneLoaded(Player player) {
        if (player.getSceneId() == 3) ensure(player.getQuestManager().getQuestById(48002));
    }

    public static boolean isFlightChallenge(Scene scene, SceneGroup group, int localId, int dataId) {
        return scene != null && scene.getId() == 3 && group != null && group.id == GROUP
                && localId == 244 && dataId == 295;
    }

    public static WorldChallenge createChallenge(Scene scene, SceneGroup group, int seconds, int tag, int count) {
        var timer = new InTimeTrigger() {
            @Override public void onBegin(WorldChallenge challenge) {
                // Challenge 295's progress template uses %1% for time; count is parameter 2.
                scene.broadcastPacket(new PacketChallengeDataNotify(challenge, 1,
                        challenge.getStartedAt() + challenge.getTimeLimit()));
            }
        };
        var markers = new TriggerGroupTriggerTrigger(String.valueOf(tag)) {
            @Override public void onGroupTrigger(WorldChallenge challenge, SceneTrigger trigger) {
                if (trigger.currentGroup != null && trigger.currentGroup.id == GROUP)
                    super.onGroupTrigger(challenge, trigger);
            }
        };
        return new FlightChallenge(scene, group, seconds, count, timer, markers);
    }

    static void stopWithQuest(GameQuest quest) {
        if (quest == null || quest.getMainQuestId() != 480 || quest.getSubQuestId() != 48002
                || (quest.getState() != QuestState.QUEST_STATE_FINISHED
                    && quest.getState() != QuestState.QUEST_STATE_FAILED)) return;
        var world = quest.getOwner().getWorld();
        var scene = world != null ? world.getSceneById(3) : null;
        if (scene != null) {
            var instance = scene.getScriptManager().getGroupInstanceById(GROUP);
            if (instance != null) instance.getLuaGroup().setDontUnload(false);
        }
        var challenge = scene != null ? scene.getChallenge() : null;
        if (challenge instanceof FlightChallenge flight)
            flight.stopQuietly(quest.getState() == QuestState.QUEST_STATE_FINISHED);
    }

    private static final class FlightChallenge extends WorldChallenge {
        private FlightChallenge(Scene scene, SceneGroup group, int seconds, int count,
                InTimeTrigger timer, TriggerGroupTriggerTrigger markers) {
            super(scene, group, 295, 244, List.of(seconds, count), seconds, count, List.of(timer, markers));
        }
        @Override public synchronized void done() {
            if (getScene().getSceneTimeSeconds() - getStartedAt() > getTimeLimit()) super.fail();
            else super.done();
        }
        @Override public synchronized void fail() { super.fail(); }
        private synchronized void stopQuietly(boolean success) {
            if (!inProgress()) return;
            setProgress(false);
            setSuccess(success);
            setFinishedTime(Math.max(0, getScene().getSceneTimeSeconds() - getStartedAt()));
            getScene().broadcastPacket(new PacketDungeonChallengeFinishNotify(this));
        }
    }
}
