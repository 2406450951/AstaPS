package emu.grasscutter.game.quest.content;

import static emu.grasscutter.game.quest.enums.QuestContent.QUEST_CONTENT_CLEAR_GROUP_MONSTER;

import emu.grasscutter.data.excels.quest.QuestData;
import emu.grasscutter.game.quest.*;
import lombok.val;

@QuestValueContent(QUEST_CONTENT_CLEAR_GROUP_MONSTER)
public class ContentClearGroupMonster extends BaseContent {

    static boolean matchesGroupEvent(int expectedGroupId, int... eventParams) {
        return expectedGroupId > 0 && eventParams.length > 0
                && eventParams[0] == expectedGroupId;
    }

    @Override
    public boolean execute(
            GameQuest quest, QuestData.QuestContentCondition condition, String paramStr, int... params) {
        if (condition == null || condition.getParam() == null
                || condition.getParam().length == 0) return false;
        val groupId = condition.getParam()[0];
        // A monster dying in some *other* group must never complete this group.
        if (!matchesGroupEvent(groupId, params)) return false;
        return quest.getOwner().getScene().getScriptManager().isClearedGroupMonsters(groupId);
    }
}
