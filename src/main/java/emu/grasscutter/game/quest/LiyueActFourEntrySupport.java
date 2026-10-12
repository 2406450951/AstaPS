package emu.grasscutter.game.quest;

import emu.grasscutter.data.GameData;
import emu.grasscutter.data.common.PointData;
import emu.grasscutter.data.excels.quest.QuestData;
import emu.grasscutter.game.quest.enums.ParentQuestState;
import emu.grasscutter.game.quest.enums.QuestContent;
import emu.grasscutter.game.quest.enums.QuestState;
import emu.grasscutter.server.packet.send.PacketScenePointUnlockNotify;
import java.util.Arrays;

/** Chapter 1104 entrances are temporary plot points, not exploration rewards. */
public final class LiyueActFourEntrySupport {
    private LiyueActFourEntrySupport() {}

    private record Entry(int mainId, int questId, int prerequisite, int end, int point, int dungeon) {}

    private static final Entry[] ENTRIES = {
        new Entry(8000, 800002, 800001, 800007, 233, 1114),
        new Entry(8003, 800309, 800307, 800311, 240, 1115)
    };

    private static Entry entryFor(GameQuest quest) {
        if (quest == null) return null;
        for (var entry : ENTRIES) {
            if (quest.getMainQuestId() == entry.mainId() && quest.getSubQuestId() == entry.questId())
                return entry;
        }
        return null;
    }

    public static boolean isEntryAction(GameQuest quest, int sceneId, int pointId) {
        var entry = entryFor(quest);
        return entry != null && sceneId == 3 && pointId == entry.point();
    }

    static boolean matchesEntryData(GameQuest quest, QuestData data, PointData point) {
        var entry = entryFor(quest);
        if (entry == null || data == null || data.getMainId() != entry.mainId()
                || data.getId() != entry.questId() || data.getFinishCond() == null
                || data.getFinishCond().size() != 1 || point == null
                || !"DungeonEntry".equals(point.getType()) || !point.isForbidSimpleUnlock()
                || point.getDungeonIds() == null
                || Arrays.stream(point.getDungeonIds()).noneMatch(id -> id == entry.dungeon())) return false;
        var condition = data.getFinishCond().get(0);
        return condition.getType() == QuestContent.QUEST_CONTENT_ENTER_DUNGEON
                && condition.getParam() != null && condition.getParam().length >= 2
                && condition.getParam()[0] == entry.dungeon()
                && condition.getParam()[1] == entry.point() && "3".equals(condition.getParamStr());
    }

    public static boolean enable(GameQuest quest) {
        var entry = entryFor(quest);
        if (entry == null || !QuestManager.isQuestingActive()) return false;
        var main = quest.getMainQuest();
        if (main == null || main.getParentQuestId() != entry.mainId() || main.isFinished()
                || main.getState() != ParentQuestState.PARENT_QUEST_STATE_NONE
                || quest.getState() != QuestState.QUEST_STATE_UNFINISHED) return false;
        var ready = main.getChildQuestById(entry.prerequisite());
        var end = main.getChildQuestById(entry.end());
        if (ready == null || ready.getState() != QuestState.QUEST_STATE_FINISHED
                || end == null || end.getState() == QuestState.QUEST_STATE_FINISHED) return false;
        var scenePoint = GameData.getScenePointEntryById(3, entry.point());
        if (!matchesEntryData(quest, quest.getQuestData(),
                scenePoint != null ? scenePoint.getPointData() : null)) return false;
        var player = main.getOwner();
        if (player == null) return false;
        boolean changed = player.getUnlockedScenePoints(3).add(entry.point());
        changed |= player.getForceLockedScenePoints(3).remove(entry.point());
        player.sendPacket(new PacketScenePointUnlockNotify(3, entry.point()));
        if (changed) player.save();
        return true;
    }

    static boolean resumeOnLogin(GameMainQuest main) {
        for (var entry : ENTRIES) {
            if (main.getParentQuestId() != entry.mainId()) continue;
            var quest = main.getChildQuestById(entry.questId());
            return quest != null && enable(quest);
        }
        return false;
    }
}
