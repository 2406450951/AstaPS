package emu.grasscutter.server.packet.send;

import static emu.grasscutter.config.Configuration.GAME_OPTIONS;
import static org.junit.jupiter.api.Assertions.*;

import com.google.gson.Gson;
import emu.grasscutter.config.ConfigContainer;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.net.proto.GetSceneAreaRspOuterClass.GetSceneAreaRsp;
import emu.grasscutter.net.proto.GetScenePointRspOuterClass.GetScenePointRsp;
import java.util.Set;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.parallel.ResourceLock;

@ResourceLock("mapFogEnabled")
final class PacketGetSceneAreaRspTest {
    private boolean previousFogEnabled;

    @BeforeEach
    void enableFog() {
        previousFogEnabled = GAME_OPTIONS.mapFogEnabled;
        GAME_OPTIONS.mapFogEnabled = true;
    }

    @AfterEach
    void restoreFog() {
        GAME_OPTIONS.mapFogEnabled = previousFogEnabled;
    }

    @Test
    void fogIsEnabledByDefaultAndOldConfigsInheritThatDefault() {
        assertTrue(new ConfigContainer.GameOptions().mapFogEnabled);
        var oldConfig = new Gson().fromJson(
                "{\"server\":{\"game\":{\"gameOptions\":{}}}}",
                ConfigContainer.class);
        assertTrue(oldConfig.server.game.gameOptions.mapFogEnabled);
        var disabled = new Gson().fromJson(
                "{\"server\":{\"game\":{\"gameOptions\":{\"mapFogEnabled\":false}}}}",
                ConfigContainer.class);
        assertFalse(disabled.server.game.gameOptions.mapFogEnabled);
    }

    @Test
    void freshPlayerSeesOnlyTheStartingAreaWithoutPersistingAnyUnlocks() throws Exception {
        var player = new Player();
        var response = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        var points = GetScenePointRsp.parseFrom(new PacketGetScenePointRsp(player, 3).getData());

        assertEquals(3, response.getSceneId());
        assertEquals(Set.of(1), Set.copyOf(response.getAreaIdListList()));
        assertEquals(Set.of(1), Set.copyOf(points.getUnlockAreaListList()));
        assertTrue(player.getUnlockedSceneAreas(3).isEmpty());
    }

    @Test
    void onlyUnlockedAreasOfTheRequestedSceneAreReturned() throws Exception {
        var player = new Player();
        player.getUnlockedSceneAreas(3).addAll(Set.of(1, 3, 7));
        player.getUnlockedSceneAreas(4).add(25);

        var mondstadt = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        var otherScene = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 4).getData());

        assertEquals(Set.of(1, 3, 7), Set.copyOf(mondstadt.getAreaIdListList()));
        assertEquals(Set.of(25), Set.copyOf(otherScene.getAreaIdListList()));
        assertEquals(Set.of(1, 3, 7), player.getUnlockedSceneAreas(3));
        assertEquals(Set.of(25), player.getUnlockedSceneAreas(4));
    }

    @Test
    void disablingFogShowsTheWholeMapButDoesNotUnlockAnything() throws Exception {
        var player = new Player();
        player.getUnlockedSceneAreas(3).add(3);
        GAME_OPTIONS.mapFogEnabled = false;

        var areas = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        var points = GetScenePointRsp.parseFrom(new PacketGetScenePointRsp(player, 3).getData());

        assertEquals(1000, areas.getAreaIdListCount());
        assertEquals(1000, points.getUnlockAreaListCount());
        assertEquals(1, areas.getAreaIdList(0));
        assertEquals(1000, areas.getAreaIdList(999));
        assertEquals(Set.of(3), player.getUnlockedSceneAreas(3));

        GAME_OPTIONS.mapFogEnabled = true;
        var restored = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        assertEquals(Set.of(3), Set.copyOf(restored.getAreaIdListList()));
    }
}
