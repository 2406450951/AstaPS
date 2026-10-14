package emu.grasscutter.command.commands;

import static org.junit.jupiter.api.Assertions.assertEquals;

import emu.grasscutter.game.quest.GameMainQuest;
import emu.grasscutter.game.quest.GameQuest;
import emu.grasscutter.game.quest.enums.QuestState;
import java.lang.reflect.Field;
import java.util.HashMap;
import java.util.List;
import org.junit.jupiter.api.Test;

public final class QuestCommandTest {
    @Test
    void listsUnfinishedQuestsWithoutDebugLogRegistration() {
        // A fresh account has no quest debug log entries, but still has active quests.
        var main = mainQuest(
                subQuest(35202, QuestState.QUEST_STATE_UNFINISHED),
                subQuest(35203, QuestState.QUEST_STATE_UNSTARTED));

        assertEquals(List.of(35202), QuestCommand.activeQuestIds(List.of(main)));
    }

    @Test
    void listsAllActiveSubquestsInOrderAndExcludesInactiveStates() {
        var first = mainQuest(
                subQuest(48002, QuestState.QUEST_STATE_UNFINISHED),
                subQuest(48001, QuestState.QUEST_STATE_FINISHED));
        var second = mainQuest(
                subQuest(35202, QuestState.QUEST_STATE_UNFINISHED),
                subQuest(35203, QuestState.QUEST_STATE_FAILED));

        assertEquals(List.of(35202, 48002), QuestCommand.activeQuestIds(List.of(first, second)));
        assertEquals(List.of(), QuestCommand.activeQuestIds(List.of()));
    }

    private static GameMainQuest mainQuest(GameQuest... quests) {
        var main = new GameMainQuest();
        var children = new HashMap<Integer, GameQuest>();
        for (var quest : quests) {
            children.put(quest.getSubQuestId(), quest);
        }
        setField(main, "childQuests", children);
        return main;
    }

    private static GameQuest subQuest(int id, QuestState state) {
        var quest = new GameQuest();
        setField(quest, "subQuestId", id);
        quest.setState(state);
        return quest;
    }

    private static void setField(Object target, String name, Object value) {
        try {
            Field field = target.getClass().getDeclaredField(name);
            field.setAccessible(true);
            field.set(target, value);
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }
}
