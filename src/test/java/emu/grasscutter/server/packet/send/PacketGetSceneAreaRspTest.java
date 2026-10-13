package emu.grasscutter.server.packet.send;

import static emu.grasscutter.config.Configuration.GAME_OPTIONS;
import static org.junit.jupiter.api.Assertions.*;

import com.google.gson.Gson;
import emu.grasscutter.config.ConfigContainer;
import emu.grasscutter.data.GameData;
import emu.grasscutter.data.binout.ScenePointEntry;
import emu.grasscutter.data.common.PointData;
import emu.grasscutter.data.excels.world.WorldAreaData;
import emu.grasscutter.game.player.Player;
import emu.grasscutter.net.proto.GetSceneAreaRspOuterClass.GetSceneAreaRsp;
import emu.grasscutter.net.proto.GetScenePointRspOuterClass.GetScenePointRsp;
import java.util.Set;
import java.util.stream.IntStream;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.parallel.ResourceLock;

@ResourceLock("mapFogEnabled")
@ResourceLock("GameData")
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
    @Test
    void legacyFullAreaSavesRecoverFromStatuesAcrossRepeatedToggles() throws Exception {
        int sceneId = 4;
        int statueId = 22;
        int waypointId = 23;
        int statueKey = (sceneId << 16) + statueId;
        int waypointKey = (sceneId << 16) + waypointId;
        var gson = new Gson();
        var statue = gson.fromJson(
                "{\"id\":22,\"areaId\":5,\"maxSpringVolume\":5000}", PointData.class);
        var waypoint = gson.fromJson(
                "{\"id\":23,\"areaId\":8,\"$type\":\"TransPointNormal\"}", PointData.class);
        var childArea = gson.fromJson(
                "{\"sceneID\":4,\"areaID1\":5,\"areaID2\":501}", WorldAreaData.class);
        int areaKey = childArea.getId();

        var scenePoints = GameData.getScenePointEntryMap();
        var worldAreas = GameData.getWorldAreaDataMap();
        var oldStatue = scenePoints.put(statueKey, new ScenePointEntry(sceneId, statue));
        var oldWaypoint = scenePoints.put(waypointKey, new ScenePointEntry(sceneId, waypoint));
        var oldArea = worldAreas.put(areaKey, childArea);
        try {
            var player = new Player();
            player.getUnlockedScenePoints(sceneId).addAll(Set.of(statueId, waypointId));
            // This is what older GetSceneAreaRsp versions persisted on every map request.
            player.getUnlockedSceneAreas(sceneId)
                    .addAll(IntStream.rangeClosed(1, 1000).boxed().toList());

            for (boolean enabled : new boolean[] {true, false, true, false, true}) {
                GAME_OPTIONS.mapFogEnabled = enabled;
                var areas = GetSceneAreaRsp.parseFrom(
                        new PacketGetSceneAreaRsp(player, sceneId).getData());
                var points = GetScenePointRsp.parseFrom(
                        new PacketGetScenePointRsp(player, sceneId).getData());

                var expected = enabled ? Set.of(5, 501)
                        : Set.copyOf(IntStream.rangeClosed(1, 1000).boxed().toList());
                assertEquals(expected, Set.copyOf(areas.getAreaIdListList()));
                assertEquals(expected, Set.copyOf(points.getUnlockAreaListList()));
                assertEquals(1000, player.getUnlockedSceneAreas(sceneId).size());
                assertTrue(player.getUnlockedScenePoints(sceneId).containsAll(
                        Set.of(statueId, waypointId)));
            }

            // A quest-locked statue cannot reveal its region while it remains locked.
            player.getForceLockedScenePoints(sceneId).add(statueId);
            assertTrue(GetSceneAreaRsp.parseFrom(
                    new PacketGetSceneAreaRsp(player, sceneId).getData())
                    .getAreaIdListList().isEmpty());
        } finally {
            if (oldStatue == null) scenePoints.remove(statueKey);
            else scenePoints.put(statueKey, oldStatue);
            if (oldWaypoint == null) scenePoints.remove(waypointKey);
            else scenePoints.put(waypointKey, oldWaypoint);
            if (oldArea == null) worldAreas.remove(areaKey);
            else worldAreas.put(areaKey, oldArea);
        }
    }

    @Test
    void legacyFullAreaSavesWithoutStatuesKeepOnlyTheStarterArea() throws Exception {
        var player = new Player();
        player.getUnlockedSceneAreas(3)
                .addAll(IntStream.rangeClosed(1, 1000).boxed().toList());

        var response = GetSceneAreaRsp.parseFrom(new PacketGetSceneAreaRsp(player, 3).getData());
        assertEquals(Set.of(1), Set.copyOf(response.getAreaIdListList()));
        assertEquals(1000, player.getUnlockedSceneAreas(3).size());
    }

}
