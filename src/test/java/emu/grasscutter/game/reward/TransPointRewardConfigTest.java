package emu.grasscutter.game.reward;

import static org.junit.jupiter.api.Assertions.*;

import com.google.gson.Gson;
import emu.grasscutter.data.common.PointData;
import emu.grasscutter.data.common.ItemParamData;
import emu.grasscutter.data.excels.dungeon.DungeonEntryData;
import java.util.Map;
import java.util.stream.Collectors;
import org.junit.jupiter.api.Test;

final class TransPointRewardConfigTest {
    private static final Gson GSON = new Gson();
    private static final String RULES = """
            version = 1
            ["kind=waypoint"]
            adventureExp = 50
            primogems = 5
            ["kind=statue"]
            adventureExp = 50
            primogems = 5
            ["kind=domain"]
            adventureExp = 50
            primogems = 5
            ["3:6"]
            adventureExp = 10
            ["7:*"]
            primogems = 5
            items = [{ id = 101692, count = 20 }]
            """;

    private static Map<Integer, Integer> items(java.util.List<ItemParamData> reward) {
        return reward.stream().collect(Collectors.toMap(ItemParamData::getId, ItemParamData::getCount));
    }

    @Test
    void selectorsReplaceRewardsByExactSceneThenKind() {
        var rules = TransPointRewardConfig.parse(RULES);
        assertEquals(Map.of(102, 10), items(rules.resolve(3, 6, TransPointRewardConfig.Kind.WAYPOINT)));
        assertEquals(Map.of(102, 50, 201, 5),
                items(rules.resolve(3, 8, TransPointRewardConfig.Kind.WAYPOINT)));
        assertEquals(Map.of(102, 50, 201, 5),
                items(rules.resolve(3, 3, TransPointRewardConfig.Kind.STATUE)));
        assertEquals(Map.of(101692, 20, 201, 5),
                items(rules.resolve(7, 2, TransPointRewardConfig.Kind.WAYPOINT)));
        assertEquals(Map.of(102, 50, 201, 5),
                items(rules.resolve(3, 48, TransPointRewardConfig.Kind.DOMAIN)));
    }

    @Test
    void exactPointOverridesSceneWildcard() {
        var text = RULES + """
                ["7:2"]
                adventureExp = 15
                """;
        var rules = TransPointRewardConfig.parse(text);
        assertEquals(Map.of(102, 15), items(rules.resolve(7, 2, TransPointRewardConfig.Kind.WAYPOINT)));
        assertEquals(Map.of(101692, 20, 201, 5),
                items(rules.resolve(7, 3, TransPointRewardConfig.Kind.WAYPOINT)));
    }

    @Test
    void unsupportedSelectorsAndBrokenConfigurationFailClosed() {
        assertThrows(IllegalArgumentException.class,
                () -> TransPointRewardConfig.parse(RULES + "[\"region=mondstadt\"]\nprimogems = 3\n"));
        assertThrows(IllegalArgumentException.class,
                () -> TransPointRewardConfig.parse(RULES.replace("adventureExp = 10", "adventureExp = -10")));
        assertThrows(IllegalArgumentException.class,
                () -> TransPointRewardConfig.parse(RULES.replace("version = 1", "version = 2")));
        assertThrows(IllegalArgumentException.class,
                () -> TransPointRewardConfig.parse(RULES.replace("id = 101692", "id = 0")));
    }

    @Test
    void onlyOrdinaryNativeTransPointsAreWaypoints() {
        var ordinary = GSON.fromJson("{\"$type\":\"SceneTransPoint\",\"gadgetId\":70110002}", PointData.class);
        var portal = GSON.fromJson("{\"$type\":\"SceneTransPoint\",\"type\":\"PORTAL\",\"unlocked\":true}", PointData.class);
        var hidden = GSON.fromJson("{\"$type\":\"SceneTransPoint\",\"gadgetId\":70110002,\"mapVisibility\":\"Never\"}", PointData.class);
        var dungeon = GSON.fromJson("{\"$type\":\"DungeonEntry\"}", PointData.class);
        assertEquals(TransPointRewardConfig.Kind.WAYPOINT,
                TransPointRewardConfig.kindOf(ordinary, false));
        var statue = GSON.fromJson(
                "{\"$type\":\"SceneTransPoint\",\"gadgetId\":70110001,\"maxSpringVolume\":5000}",
                PointData.class);
        assertEquals(TransPointRewardConfig.Kind.STATUE,
                TransPointRewardConfig.kindOf(statue, true));
        var obfuscatedStatue = GSON.fromJson(
                "{\"$type\":\"KDEHKECBDBO\",\"gadgetId\":70130009}", PointData.class);
        assertEquals(TransPointRewardConfig.Kind.STATUE,
                TransPointRewardConfig.kindOf(obfuscatedStatue, true));
        assertNull(TransPointRewardConfig.kindOf(portal, false));
        assertNull(TransPointRewardConfig.kindOf(hidden, false));
        assertNull(TransPointRewardConfig.kindOf(dungeon, false));
    }

    @Test
    void domainNativeRewardIdIsEligibilityNotAmount() {
        var domain = GSON.fromJson(
                "{\"id\":1,\"sceneId\":3,\"dungeonEntryId\":48,\"rewardDataId\":220404}",
                DungeonEntryData.class);
        assertEquals(3, domain.getSceneId());
        assertEquals(48, domain.getDungeonEntryId());
        assertEquals(220404, domain.getRewardDataId());
    }
}
