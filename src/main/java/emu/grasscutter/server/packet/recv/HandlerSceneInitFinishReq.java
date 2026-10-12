package emu.grasscutter.server.packet.recv;

import emu.grasscutter.Grasscutter;
import emu.grasscutter.config.ConfigContainer;
import emu.grasscutter.config.Configuration;
import emu.grasscutter.game.dungeons.DomainDungeonHelper;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.game.player.Player.SceneLoadState;
import emu.grasscutter.game.player.WidgetPetHelper;
import emu.grasscutter.net.packet.*;
import emu.grasscutter.net.proto.SceneInitFinishReqOuterClass.SceneInitFinishReq;
import emu.grasscutter.server.game.GameSession;
import emu.grasscutter.server.packet.send.*;
import emu.grasscutter.utils.LuaShell;
import emu.grasscutter.utils.WatermarkGradientHelper;
import emu.grasscutter.utils.WatermarkUtils;

@Opcodes(PacketOpcodes.SceneInitFinishReq)
public class HandlerSceneInitFinishReq extends PacketHandler {

    @Override
    public void handle(GameSession session, byte[] header, byte[] payload) throws Exception {
        SceneInitFinishReq req = SceneInitFinishReq.parseFrom(payload);

        var player = session.getPlayer();
        var world = player.getWorld();

        try {
            WatermarkGradientHelper.setCurrentPlayer(player);

            // Preload follower-pet embryos before team AbilityControlBlock is sent
            WidgetPetHelper.preloadEquippedWidget(player);

            try {
                // Scrub dive leftovers before enter-scene ability blocks (login / teleport).
                emu.grasscutter.game.player.DiveAbilityHelper.onTeamOrSceneRebuild(player);
            } catch (Throwable ignored) {
            }

            session.send(new PacketServerTimeNotify());
            session.send(new PacketWorldPlayerInfoNotify(world));
            session.send(new PacketWorldDataNotify(world));
            session.send(new PacketPlayerWorldSceneInfoListNotify(player));
            session.send(new PacketSceneForceUnlockNotify(1, true));
            session.send(new PacketHostPlayerNotify(world));
            session.send(new PacketSceneDataNotify(player));

            session.send(new PacketSceneTimeNotify(player));
            session.send(new PacketPlayerGameTimeNotify(player));
            session.send(new PacketPlayerEnterSceneInfoNotify(player));
            int moonPhaseCount = (int) player.getTeamManager().getActiveTeam().stream()
                    .filter(e -> PacketPlayerEnterSceneInfoNotify.getMoonphaseIds().contains(e.getAvatar().getAvatarId()))
                    .count();
            session.send(new PacketTeamMoonPhaseChangeNotify(moonPhaseCount));
            int hexenzirkelCount = (int) player.getTeamManager().getActiveTeam().stream()
                    .filter(e -> PacketPlayerEnterSceneInfoNotify.getHexenzirkelIds().contains(e.getAvatar().getAvatarId()))
                    .count();
            session.send(new PacketTeamHexenzirkelChangeNotify(hexenzirkelCount));
            if (!player.getQuestManager().restoreActiveQuestWeather(player.getSceneId())) {
                session.send(new PacketSceneAreaWeatherNotify(player));
            }
            session.send(new PacketScenePlayerInfoNotify(world));
            session.send(new PacketSceneTeamUpdateNotify(player));

            session.send(new PacketSyncTeamEntityNotify(player));
            session.send(new PacketSyncScenePlayTeamEntityNotify(player));

            // Follower pets need the attach-ability notify again after scene ability block is set
            WidgetPetHelper.syncEquippedWidget(player);

            session.send(new PacketSceneInitFinishRsp(player));
            session.send(buildWatermarkPacket());

            player.setSceneLoadState(SceneLoadState.INIT);

            player.getScene().playerSceneInitialized(player);
        } finally {
            WatermarkGradientHelper.clearCurrentPlayer();
        }
    }

    /**
     * Builds the watermark payload.
     *
     * <p>The default path is the {@link LuaShell} login chunk, which rewrites only the {@code "UID:"}
     * prefix and so keeps the player's UID on screen. It is loaded once at startup and can be
     * replaced by dropping a compiled chunk at {@code lua/login.luac}.
     *
     * <p>A configured {@code watermark.text} still wins: that path overwrites the whole watermark
     * and is the only way to show per-player text, which the shared shell cannot do.
     */
    private static BasePacket buildWatermarkPacket() {
        ConfigContainer.GameOptions.WatermarkOptions w = Configuration.GAME_OPTIONS.watermark;
        if (!w.enabled) {
            return new PacketWindSeedUID();
        }

        if (w.text == null || w.text.isBlank()) {
            byte[] shell = LuaShell.getLuaShell();
            if (shell != null && shell.length >= 6) {
                return new PacketWindSeedClientNotify(shell);
            }
            return new PacketWindSeedUID();
        }

        Player player = WatermarkGradientHelper.getCurrentPlayer();
        String text = WatermarkGradientHelper.buildDisplayText(w, player);
        String colored = WatermarkGradientHelper.applyGradient(w, text);
        if (WatermarkUtils.fits(colored)) {
            return new PacketWindSeedClientNotify(WatermarkUtils.buildLuac(colored));
        }
        if (WatermarkUtils.fits(text)) {
            return new PacketWindSeedClientNotify(WatermarkUtils.buildLuac(text));
        }
        Grasscutter.getLogger().warn("Watermark text too long; using the login Lua shell instead.");
        byte[] shell = LuaShell.getLuaShell();
        return shell != null && shell.length >= 6
                ? new PacketWindSeedClientNotify(shell)
                : new PacketWindSeedUID();
    }
}
