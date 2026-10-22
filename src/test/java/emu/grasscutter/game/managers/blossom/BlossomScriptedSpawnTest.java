package emu.grasscutter.game.managers.blossom;

import static org.junit.jupiter.api.Assertions.*;

import emu.grasscutter.game.world.SpawnDataEntry;
import emu.grasscutter.game.world.SpawnDataEntry.GridBlockId;
import emu.grasscutter.game.world.SpawnDataEntry.SpawnGroupEntry;
import emu.grasscutter.utils.JsonUtils;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import org.junit.jupiter.api.Test;

final class BlossomScriptedSpawnTest {
    @Test
    void scriptedSpawnsSelectOnlyAuthoredLeyLineOutcropsInTheCurrentScene() {
        var mondstadt = group(3, 133000001,
                "{\"gadgetId\":70360056,\"configId\":1,\"pos\":{\"x\":1,\"y\":2,\"z\":3}}",
                "{\"gadgetId\":70360057,\"configId\":2,\"pos\":{\"x\":4,\"y\":5,\"z\":6}}",
                "{\"gadgetId\":70210109,\"configId\":3,\"pos\":{\"x\":7,\"y\":8,\"z\":9}}");
        var elsewhere = group(4, 144000001,
                "{\"gadgetId\":70360056,\"configId\":4,\"pos\":{\"x\":1,\"y\":2,\"z\":3}}");
        var sources = new HashMap<GridBlockId, ArrayList<SpawnDataEntry>>();
        sources.put(new GridBlockId(3, 0, 0, 0), new ArrayList<>(mondstadt.getSpawns()));
        sources.put(new GridBlockId(4, 0, 0, 0), new ArrayList<>(elsewhere.getSpawns()));

        var selected = BlossomManager.scriptedCandidates(sources, 3);
        assertEquals(List.of(70360056, 70360057),
                selected.stream().map(SpawnDataEntry::getGadgetId).toList());
        assertTrue(BlossomManager.scriptedCandidates(sources, 5).isEmpty());
    }

    private static SpawnGroupEntry group(int sceneId, int groupId, String... spawns) {
        var group = JsonUtils.decode(
                "{\"sceneId\":" + sceneId + ",\"groupId\":" + groupId
                        + ",\"spawns\":[" + String.join(",", spawns) + "]}", SpawnGroupEntry.class);
        group.getSpawns().forEach(spawn -> spawn.setGroup(group));
        return group;
    }
}
