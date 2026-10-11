package emu.grasscutter.game.quest.content;

import static emu.grasscutter.game.quest.enums.QuestContent.QUEST_CONTENT_CLEAR_GROUP_MONSTER;

import emu.grasscutter.data.excels.quest.QuestData;
import emu.grasscutter.game.quest.*;
import lombok.val;

@QuestValueContent(QUEST_CONTENT_CLEAR_GROUP_MONSTER)
public class ContentClearGroupMonster extends BaseContent {

    @Override
    public boolean execute(
            GameQuest quest, QuestData.QuestContentCondition condition, String paramStr, int... params) {
        val groupId = condition.getParam()[0];

        boolean jadeBattle = quest.getMainQuestId() == 1021
                && (quest.getSubQuestId() == 102102 || quest.getSubQuestId() == 102109)
                || quest.getMainQuestId() == 1022 && quest.getSubQuestId() == 102205;
        if (jadeBattle) {
            var scene = quest.getOwner().getScene();
            if (scene == null || scene.getId() != 3 || params.length == 0 || params[0] != groupId)
                return false;
            var instance = scene.getScriptManager().getGroupInstanceById(groupId);
            // Other nearby kills cannot complete an empty default suite before the task spawns it.
            if (instance == null || instance.getActiveSuiteId() < 2) return false;
        }
        return quest.getOwner().getScene().getScriptManager().isClearedGroupMonsters(groupId);
    }
}
