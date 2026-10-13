package emu.grasscutter.game.quest;

import emu.grasscutter.data.GameData;
import emu.grasscutter.data.excels.quest.QuestData;
import emu.grasscutter.game.quest.enums.LogicType;
import emu.grasscutter.game.quest.enums.ParentQuestState;
import emu.grasscutter.game.quest.enums.QuestCond;
import emu.grasscutter.game.quest.enums.QuestContent;
import emu.grasscutter.game.quest.enums.QuestState;
import emu.grasscutter.server.packet.send.PacketScenePointUnlockNotify;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Set;

/** Rechecks Amber and Razor's reviewed gates when an older character missed their events. */
public final class MondstadtStoryQuestSupport {
    private static final Set<Integer> MAIN_QUEST_IDS =
            Set.of(427, 451, 452, 453, 454, 480, 481, 482, 484, 485);

    private MondstadtStoryQuestSupport() {}

    static boolean isRelevantEvent(QuestCond type, int[] params) {
        if (params == null || params.length == 0) return false;
        if (type == QuestCond.QUEST_COND_PLAYER_LEVEL_EQUAL_GREATER) return true;
        if (type != QuestCond.QUEST_COND_STATE_EQUAL
                && type != QuestCond.QUEST_COND_STATE_NOT_EQUAL) return false;
        if (params[0] == 30904 || params[0] == 39604) return true;
        var data = GameData.getQuestDataMap().get(params[0]);
        return data != null && MAIN_QUEST_IDS.contains(data.getMainId());
    }

    static void onConditionEvent(QuestManager manager, QuestCond type, int[] params) {
        if (isRelevantEvent(type, params)) resumeOnLogin(manager);
    }

    static boolean canResume(QuestManager manager, QuestData data) {
        if (data == null || !MAIN_QUEST_IDS.contains(data.getMainId())
                || manager.wasSubQuestStarted(data)) return false;
        var parent = manager.getMainQuestById(data.getMainId());
        if (parent != null && (parent.isFinished()
                || parent.getState() == ParentQuestState.PARENT_QUEST_STATE_FINISHED)) return false;
        var conditions = data.getAcceptCond();
        if (conditions == null || conditions.isEmpty()) return false;
        // Do not resurrect dormant test branches or use a missing/unsupported gate as a match.
        return conditions.stream().allMatch(condition -> {
            var type = condition.getType();
            var params = condition.getParam();
            return (type == QuestCond.QUEST_COND_STATE_EQUAL
                    || type == QuestCond.QUEST_COND_STATE_NOT_EQUAL
                    || type == QuestCond.QUEST_COND_PLAYER_LEVEL_EQUAL_GREATER)
                    && params != null && params.length >= 2
                    && params[0] > 0 && params[0] != 99902;
        });
    }

    static void resumeOnLogin(QuestManager manager) {
        if (!QuestManager.isQuestingActive()) return;
        var candidates = GameData.getQuestDataMap().values().stream()
                .filter(data -> MAIN_QUEST_IDS.contains(data.getMainId()))
                .sorted(Comparator.comparingInt(QuestData::getMainId)
                        .thenComparingInt(QuestData::getOrder))
                .toList();
        for (var data : candidates) {
            if (data.getSubId() == 48002 || data.getSubId() == 42702 || data.getSubId() == 42705)
                AmberFlightExamSupport.ensure(manager.getQuestById(data.getSubId()));
            if (data.getSubId() == 48501) {
                var existing = manager.getQuestById(48501);
                if (existing != null) enableDungeonEntry(existing);
            }
            if (isEligible(manager, data)) manager.addQuest(data);
        }
    }

    static boolean canContinueAmberDungeon(GameQuest quest, emu.grasscutter.data.excels.dungeon.DungeonData dungeon) {
        if (!QuestManager.isQuestingActive() || quest == null || dungeon == null
                || quest.getMainQuestId() != 485 || quest.getSubQuestId() != 48510
                || quest.getState() != QuestState.QUEST_STATE_UNFINISHED
                || dungeon.getId() != 2007 || dungeon.getSceneId() != 1015
                || dungeon.getPassCond() != 35 || dungeon.getPassJumpDungeon() != 2006) return false;
        var main = quest.getMainQuest();
        if (main == null || main.isFinished() || main.getState() == ParentQuestState.PARENT_QUEST_STATE_FINISHED)
            return false;
        for (int id : new int[] {48509, 48512}) {
            var prerequisite = main.getChildQuestById(id);
            if (prerequisite == null || prerequisite.getState() != QuestState.QUEST_STATE_FINISHED) return false;
        }
        return true;
    }

    /** Continue an earned segment after scene loading; login returns dungeon players to their entry point. */
    public static void onSceneLoaded(emu.grasscutter.game.player.Player player) {
        if (player.getSceneLoadState() != emu.grasscutter.game.player.Player.SceneLoadState.LOADED
                || (player.getSceneId() != 1015 && player.getSceneId() != 3)) return;
        var scene = player.getScene();
        var quest = player.getQuestManager().getQuestById(48510);
        var data = GameData.getDungeonDataMap().get(2007);
        if (scene == null || !canContinueAmberDungeon(quest, data)) return;
        scene.runWhenFinished(() -> scene.getScheduler().scheduleDelayedTask(() -> {
            if (player.getScene() != scene
                    || player.getSceneLoadState() != emu.grasscutter.game.player.Player.SceneLoadState.LOADED
                    || !canContinueAmberDungeon(quest, data)) return;
            if (scene.getId() == 3) {
                // Only recover at the original unlocked entry, not from an unrelated world location.
                if (emu.grasscutter.game.dungeons.DomainDungeonHelper.canEnterDungeonFromOpenWorld(player, 102, 2007))
                    player.getServer().getDungeonSystem().enterDungeon(player, 102, 2006, true);
                return;
            }
            var manager = scene.getDungeonManager();
            if (manager == null || manager.getDungeonData().getId() != 2007) return;
            if (manager.isFinishedSuccessfully()) {
                player.getServer().getDungeonSystem().enterDungeon(player, 0, 2006, false);
            } else {
                // Replay completion evidence, not a quest finish or a synthetic dungeon-entry event.
                scene.triggerDungeonEvent(
                        emu.grasscutter.game.dungeons.enums.DungeonPassConditionType.DUNGEON_COND_FINISH_QUEST, 48512);
            }
        }, 1));
    }

    static boolean isEligible(QuestManager manager, QuestData data) {
        if (!canResume(manager, data)) return false;
        var conditions = data.getAcceptCond();
        var progress = new int[conditions.size()];
        var system = manager.getPlayer().getServer().getQuestSystem();
        for (int i = 0; i < conditions.size(); i++) {
            var condition = conditions.get(i);
            progress[i] = system.triggerCondition(manager.getPlayer(), data, condition,
                    condition.getParamStr(), condition.getParam()) ? 1 : 0;
        }
        return LogicType.calculate(data.getAcceptCondComb(), progress);
    }

    /** Point 102 is a temporary plot entrance, not a reward-bearing exploration waypoint. */
    static boolean enableDungeonEntry(GameQuest quest) {
        if (!QuestManager.isQuestingActive() || quest == null
                || quest.getMainQuestId() != 485 || quest.getSubQuestId() != 48501
                || quest.getState() != QuestState.QUEST_STATE_UNFINISHED) return false;
        var main = quest.getMainQuest();
        if (main == null || main.isFinished()
                || main.getState() == ParentQuestState.PARENT_QUEST_STATE_FINISHED) return false;
        var player = main.getOwner();
        var ready = player.getQuestManager().getQuestById(48423);
        var end = main.getChildQuestById(48508);
        if (ready == null || ready.getState() != QuestState.QUEST_STATE_FINISHED
                || (end != null && end.getState() == QuestState.QUEST_STATE_FINISHED)) return false;
        var data = quest.getQuestData();
        var finish = data != null ? data.getFinishCond() : null;
        if (data == null || data.getMainId() != 485 || finish == null || finish.size() != 1) return false;
        var condition = finish.get(0);
        if (condition.getType() != QuestContent.QUEST_CONTENT_ENTER_DUNGEON
                || condition.getParam() == null || condition.getParam().length < 2
                || condition.getParam()[0] != 2007 || condition.getParam()[1] != 102
                || !"3".equals(condition.getParamStr())) return false;
        var entry = GameData.getScenePointEntryById(3, 102);
        var point = entry != null ? entry.getPointData() : null;
        if (point == null || !"DungeonEntry".equals(point.getType()) || !point.isForbidSimpleUnlock()
                || point.getDungeonIds() == null
                || Arrays.stream(point.getDungeonIds()).noneMatch(id -> id == 2007)) return false;
        boolean changed = player.getUnlockedScenePoints(3).add(102);
        changed |= player.getForceLockedScenePoints(3).remove(102);
        player.sendPacket(new PacketScenePointUnlockNotify(3, 102));
        if (changed) player.save();
        return true;
    }
}
