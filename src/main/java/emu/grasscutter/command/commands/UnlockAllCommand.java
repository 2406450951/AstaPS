package emu.grasscutter.command.commands;

import static emu.grasscutter.utils.lang.Language.translate;

import emu.grasscutter.command.Command;
import emu.grasscutter.command.CommandHandler;
import emu.grasscutter.command.CommandOutput;
import emu.grasscutter.data.GameData;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.player.PlayerProgressManager;
import emu.grasscutter.server.packet.send.PacketAvatarDataNotify;
import emu.grasscutter.server.packet.send.PacketOpenStateUpdateNotify;
import emu.grasscutter.server.packet.send.PacketSceneAreaUnlockNotify;
import emu.grasscutter.server.packet.send.PacketScenePointUnlockNotify;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.IntStream;
import picocli.CommandLine;

@Command(
        label = "unlock",
        permission = "player.unlockall",
        permissionTargeted = "player.unlockall.others")
public final class UnlockAllCommand implements CommandHandler {
    private static final List<Integer> SCENE_AREAS = IntStream.range(1, 1000).boxed().toList();

    @Override
    public CommandLine createCommandLine(Player sender, Player targetPlayer) {
        var commandLine = new CommandLine(new Root(sender));
        commandLine.addSubcommand("all", new All(sender, targetPlayer));
        return commandLine;
    }

    // Existing language keys and permission strings remain compatible.
    @Override
    public String getDescriptionKey() {
        return "commands.unlockall.description";
    }

    @CommandLine.Command(name = "unlock")
    private final class Root implements Runnable {
        private final Player sender;

        private Root(Player sender) {
            this.sender = sender;
        }

        @Override
        public void run() {
            UnlockAllCommand.this.sendUsageMessage(sender);
        }
    }

    @CommandLine.Command(name = "all")
    private static final class All implements Runnable {
        private final Player sender;
        private final Player targetPlayer;

        private All(Player sender, Player targetPlayer) {
            this.sender = sender;
            this.targetPlayer = targetPlayer;
        }

        @Override
        public void run() {
            for (var state : GameData.getOpenStateList()) {
                if (PlayerProgressManager.BLACKLIST_OPEN_STATES.contains(state.getId())) {
                    continue;
                }
                if (targetPlayer.getProgressManager().getOpenState(state.getId()) == 0) {
                    targetPlayer.getOpenStates().put(state.getId(), 1);
                }
            }
            targetPlayer.sendPacket(new PacketOpenStateUpdateNotify(targetPlayer));

            GameData.getScenePointsPerScene()
                    .forEach(
                            (sceneId, scenePoints) -> {
                                var points = new ArrayList<Integer>();
                                for (var pointId : scenePoints) {
                                    var entry = GameData.getScenePointEntryById(sceneId, pointId);
                                    if (entry == null || entry.getPointData() == null) {
                                        continue;
                                    }
                                    var data = entry.getPointData();
                                    if (data.isForbidSimpleUnlock()) {
                                        continue;
                                    }
                                    if ("SceneBuildingPoint".equals(data.getType()) && !data.isUnlocked()) {
                                        continue;
                                    }
                                    points.add(pointId);
                                }
                                targetPlayer.getUnlockedScenePoints(sceneId).addAll(points);
                                targetPlayer.getUnlockedSceneAreas(sceneId).addAll(SCENE_AREAS);
                            });

            int currentScene = targetPlayer.getSceneId();
            targetPlayer.sendPacket(
                    new PacketScenePointUnlockNotify(
                            currentScene, targetPlayer.getUnlockedScenePoints(currentScene)));
            targetPlayer.sendPacket(
                    new PacketSceneAreaUnlockNotify(
                            currentScene, targetPlayer.getUnlockedSceneAreas(currentScene)));

            GameData.getAvatarFlycloakDataMap().keySet().forEach(targetPlayer.getFlyCloakList()::add);
            GameData.getAvatarTraceEffectDataMap()
                    .keySet()
                    .forEach(targetPlayer.getTraceEffectList()::add);

            var fetterEntries = GameData.getFetterDataEntries();
            for (var avatar : targetPlayer.getAvatars().getAvatars().values()) {
                var dataFetters = fetterEntries.get(avatar.getAvatarId());
                if (dataFetters != null) {
                    List<Integer> current = avatar.getFetterList();
                    if (current == null) {
                        avatar.setFetterList(new ArrayList<>(dataFetters));
                    } else {
                        for (int fetterId : dataFetters) {
                            if (!current.contains(fetterId)) {
                                current.add(fetterId);
                            }
                        }
                    }
                }
                avatar.recalcStats(true);
                avatar.save();
            }
            targetPlayer.sendPacket(new PacketAvatarDataNotify(targetPlayer));

            // Scene tags are intentionally left unchanged.
            // Many tags represent mutually exclusive quest/activity world states. Enabling all of
            // them simultaneously can make the client load incompatible terrain variants and produce
            // missing ground/collision. Use /tag reset or /tag add <id> explicitly instead.

            targetPlayer.save();

            CommandOutput.sendMessage(
                    sender, translate(sender, "commands.unlockall.success", targetPlayer.getNickname()));
            CommandOutput.sendMessage(sender, "Scene tags left unchanged.");
        }
    }
}
