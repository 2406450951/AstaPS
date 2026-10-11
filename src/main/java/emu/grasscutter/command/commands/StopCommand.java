package emu.grasscutter.command.commands;

import static emu.grasscutter.utils.lang.Language.translate;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.command.Command;
import emu.grasscutter.command.CommandHandler;
import emu.grasscutter.command.CommandOutput;
import emu.grasscutter.game.player.Player;
import picocli.CommandLine;

@Command(
        label = "stop",
        aliases = {"shutdown"},
        permission = "server.stop",
        targetRequirement = Command.TargetRequirement.NONE)
public final class StopCommand implements CommandHandler {
    static final int NORMAL_EXIT_CODE = 0;
    @Override
    public CommandLine createCommandLine(Player sender, Player targetPlayer) {
        return new CommandLine(new Args(sender));
    }

    @CommandLine.Command(name = "stop")
    private static final class Args implements Runnable {
        private final Player sender;

        private Args(Player sender) {
            this.sender = sender;
        }

        @Override
        public void run() {
            CommandOutput.sendMessage(null, translate("commands.stop.success"));
            for (Player player : Grasscutter.getGameServer().getPlayers().values()) {
                CommandOutput.sendMessage(player, translate(player, "commands.stop.success"));
            }
            System.exit(NORMAL_EXIT_CODE);
        }
    }
}
