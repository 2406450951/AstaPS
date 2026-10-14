package emu.grasscutter.command.commands;

import static emu.grasscutter.utils.lang.Language.translate;

import emu.grasscutter.command.*;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.quest.GameQuest;
import emu.grasscutter.game.quest.QuestManager;
import emu.grasscutter.game.quest.enums.*;
import java.util.*;
import java.util.stream.Collectors;
import picocli.CommandLine;
import picocli.CommandLine.Parameters;

@Command(
        label = "quest",
        aliases = {"q"},
        permission = "player.quest",
        permissionTargeted = "player.quest.others")
public final class QuestCommand implements CommandHandler {
    private record QuestId(int value) {}

    private record ForceFinishTarget(Integer questId) {
        private boolean all() {
            return questId == null;
        }
    }

    @Override
    public CommandLine createCommandLine(Player sender, Player targetPlayer) {
        var commandLine = new CommandLine(new Root(sender));
        commandLine.setExpandAtFiles(false);

        commandLine.addSubcommand("forcefinish", new ForceFinish(sender, targetPlayer));
        commandLine.addSubcommand("add", new Add(sender, targetPlayer));
        commandLine.addSubcommand("finish", new Finish(sender, targetPlayer));
        commandLine.addSubcommand("running", new Running(sender, targetPlayer));
        commandLine.addSubcommand("talking", new Talking(sender, targetPlayer));
        commandLine.addSubcommand("dungeons", new Dungeons(sender, targetPlayer));
        commandLine.addSubcommand("debug", new Debug(sender, targetPlayer));
        commandLine.addSubcommand("triggers", new Triggers(sender, targetPlayer));
        commandLine.addSubcommand("grouptriggers", new GroupTriggers(sender, targetPlayer));
        commandLine.addSubcommand("list", new ListQuests(sender, targetPlayer));
        CommandHandler.registerConverterTree(commandLine,
                QuestId.class,
                value -> {
                    try {
                        return new QuestId(Integer.parseInt(value));
                    } catch (NumberFormatException ignored) {
                        throw new CommandLine.TypeConversionException(
                                translate(sender, "commands.quest.invalid_id"));
                    }
                });
        CommandHandler.registerConverterTree(commandLine,
                ForceFinishTarget.class,
                value -> {
                    if ("all".equalsIgnoreCase(value)) return new ForceFinishTarget(null);
                    try {
                        return new ForceFinishTarget(Integer.parseInt(value));
                    } catch (NumberFormatException ignored) {
                        throw new CommandLine.TypeConversionException(
                                translate(sender, "commands.quest.invalid_id"));
                    }
                });

        return commandLine;
    }

    @picocli.CommandLine.Command(name = "quest")
    private final class Root implements Runnable {
        private final Player sender;

        private Root(Player sender) {
            this.sender = sender;
        }

        @Override
        public void run() {
            QuestCommand.this.sendUsageMessage(sender);
        }
    }

    private abstract class QuestIdCommand implements Runnable {
        protected final Player sender;
        protected final Player targetPlayer;

        @Parameters(index = "0", paramLabel = "<questId>")
        protected QuestId questId;

        private QuestIdCommand(Player sender, Player targetPlayer) {
            this.sender = sender;
            this.targetPlayer = targetPlayer;
        }
    }

    @picocli.CommandLine.Command(name = "forcefinish")
    private final class ForceFinish implements Runnable {
        private final Player sender;
        private final Player targetPlayer;

        @Parameters(index = "0", paramLabel = "<questId|all>")
        private ForceFinishTarget selection;

        private ForceFinish(Player sender, Player targetPlayer) {
            this.sender = sender;
            this.targetPlayer = targetPlayer;
        }

        @Override
        public void run() {
            var ids =
                    selection.all()
                            ? emu.grasscutter.game.quest.ForcedQuests.allMainQuests()
                            : List.of(selection.questId());
            int added = emu.grasscutter.game.quest.ForcedQuests.apply(targetPlayer, ids);
            CommandOutput.sendMessage(
                    sender,
                    "Force-finished " + ids.size() + " main quest(s), " + added + " newly."
                            + " This is saved and re-sent on every login.");
        }
    }

    @picocli.CommandLine.Command(name = "add")
    private final class Add extends QuestIdCommand {
        private Add(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            if (!QuestManager.isQuestingActive()) {
                CommandOutput.sendMessage(sender, translate(sender, "commands.quest.questing_off"));
                return;
            }

            var quest = targetPlayer.getQuestManager().addQuest(questId.value());
            if (quest != null) {
                CommandOutput.sendMessage(
                        sender, translate(sender, "commands.quest.added", questId.value()));
                return;
            }
            CommandOutput.sendMessage(sender, translate(sender, "commands.quest.not_found"));
        }
    }

    @picocli.CommandLine.Command(name = "finish")
    private final class Finish extends QuestIdCommand {
        private Finish(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            var quest = targetPlayer.getQuestManager().getQuestById(questId.value());
            if (quest == null) {
                CommandOutput.sendMessage(sender, translate(sender, "commands.quest.not_found"));
                return;
            }
            quest.finish();
            CommandOutput.sendMessage(
                    sender, translate(sender, "commands.quest.finished", questId.value()));
        }
    }

    @picocli.CommandLine.Command(name = "running")
    private final class Running extends QuestIdCommand {
        private Running(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            var quest = targetPlayer.getQuestManager().getQuestById(questId.value());
            if (quest == null) {
                CommandOutput.sendMessage(sender, translate(sender, "commands.quest.not_found"));
                return;
            }

            CommandOutput.sendMessage(
                    sender,
                    translate(
                            sender,
                            "commands.quest.running",
                            questId.value(),
                            translate(
                                    sender,
                                    switch (quest.state) {
                                        case QUEST_STATE_NONE, NONE -> "commands.quest.state.none";
                                        case QUEST_STATE_UNSTARTED, UNSTARTED ->
                                                "commands.quest.state.unstarted";
                                        case QUEST_STATE_UNFINISHED, UNFINISHED ->
                                                "commands.quest.state.unfinished";
                                        case QUEST_STATE_FINISHED, FINISHED ->
                                                "commands.quest.state.finished";
                                        case QUEST_STATE_FAILED, FAILED -> "commands.quest.state.failed";
                                    }),
                            quest.getState().getValue()));
        }
    }

    @picocli.CommandLine.Command(name = "talking")
    private final class Talking extends QuestIdCommand {
        private Talking(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            var mainQuest = targetPlayer.getQuestManager().getMainQuestByTalkId(questId.value());
            if (mainQuest == null) {
                CommandOutput.sendMessage(sender, translate(sender, "commands.quest.not_found"));
                return;
            }

            var talk = mainQuest.getTalks().get(questId.value());
            CommandOutput.sendMessage(
                    sender,
                    translate(
                            sender,
                            "commands.quest.talking",
                            questId.value(),
                            talk == null
                                    ? translate(sender, "commands.quest.state.not_exists")
                                    : translate(sender, "commands.quest.state.exists"),
                            mainQuest.getParentQuestId(),
                            mainQuest.getState().getValue()));
        }
    }

    @picocli.CommandLine.Command(name = "dungeons")
    private final class Dungeons implements Runnable {
        private final Player sender;
        private final Player targetPlayer;

        private Dungeons(Player sender, Player targetPlayer) {
            this.sender = sender;
            this.targetPlayer = targetPlayer;
        }

        @Override
        public void run() {
            var dungeons = targetPlayer.getPlayerProgress().getCompletedDungeons();
            CommandOutput.sendMessage(
                    sender,
                    "Dungeons completed: "
                            + String.join(", ", dungeons.intStream().mapToObj(String::valueOf).toList()));
        }
    }

    @picocli.CommandLine.Command(name = "debug")
    private final class Debug extends QuestIdCommand {
        private Debug(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            var loggedQuests = targetPlayer.getQuestManager().getLoggedQuests();
            var shouldAdd = !loggedQuests.contains(questId.value());
            if (shouldAdd) loggedQuests.add(questId.value());
            else loggedQuests.remove(questId.value());

            CommandOutput.sendMessage(
                    sender,
                    "Quest %s will %s."
                            .formatted(
                                    questId.value(),
                                    shouldAdd ? "now be logged" : "no longer be logged"));
        }
    }

    @picocli.CommandLine.Command(name = "triggers")
    private final class Triggers extends QuestIdCommand {
        private Triggers(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            var quest = targetPlayer.getQuestManager().getQuestById(questId.value());
            if (quest == null) {
                CommandOutput.sendMessage(sender, translate(sender, "commands.quest.not_found"));
                return;
            }
            CommandOutput.sendMessage(
                    sender,
                    "Triggers registered for %s: %s."
                            .formatted(
                                    questId.value(),
                                    String.join(", ", quest.getTriggers().keySet())));
        }
    }

    @picocli.CommandLine.Command(name = "grouptriggers")
    private final class GroupTriggers extends QuestIdCommand {
        private GroupTriggers(Player sender, Player targetPlayer) {
            super(sender, targetPlayer);
        }

        @Override
        public void run() {
            if (targetPlayer.getScene() == null) {
                CommandOutput.sendMessage(sender, "Target player has no active scene.");
                return;
            }
            var scriptManager = targetPlayer.getScene().getScriptManager();
            var group = scriptManager.getGroupById(questId.value());
            if (group == null) {
                CommandOutput.sendMessage(sender, "The group does not exist.");
                return;
            }

            CommandOutput.sendMessage(
                    sender,
                    group.triggers.entrySet().stream()
                            .map(entry -> "%s: %s".formatted(entry.getKey(), entry.getValue()))
                            .collect(Collectors.joining(", ")));
        }
    }

    // The debug log is optional and must never hide unfinished quests from the command.
    static List<Integer> activeQuestIds(Collection<emu.grasscutter.game.quest.GameMainQuest> mainQuests) {
        return mainQuests.stream()
                .flatMap(main -> main.getActiveQuests().stream())
                .map(GameQuest::getSubQuestId)
                .sorted()
                .toList();
    }

    @picocli.CommandLine.Command(name = "list")
    private final class ListQuests implements Runnable {
        private final Player sender;
        private final Player targetPlayer;

        private ListQuests(Player sender, Player targetPlayer) {
            this.sender = sender;
            this.targetPlayer = targetPlayer;
        }

        @Override
        public void run() {
            var allQuestIds = activeQuestIds(targetPlayer.getQuestManager().getActiveMainQuests());

            CommandOutput.sendMessage(
                    sender,
                    "Quests: "
                            + (allQuestIds.isEmpty()
                                    ? "(no active quests)"
                                    : allQuestIds.stream()
                                            .map(String::valueOf)
                                            .collect(Collectors.joining(", "))));
        }
    }
}
