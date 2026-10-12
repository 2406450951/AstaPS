package emu.grasscutter.data.excels;

import static org.junit.jupiter.api.Assertions.*;

import com.google.gson.Gson;
import emu.grasscutter.data.GameData;
import emu.grasscutter.data.common.ItemParamData;
import java.util.Map;
import java.util.stream.Collectors;
import org.junit.jupiter.api.Test;

final class TransPointRewardDataTest {
    private static final Gson GSON = new Gson();

    @Test
    void sceneAndPointFormTheResourceKey() {
        var point = GSON.fromJson(
                "{\"sceneId\":3,\"pointId\":6,\"rewardId\":220310}", TransPointRewardData.class);
        assertEquals((3 << 16) + 6, point.getId());
        assertEquals(220310, point.getRewardId());
        assertSame(GameData.getTransPointRewardDataMap(),
                GameData.getMapByResourceDef(TransPointRewardData.class));
        assertNotEquals(point.getId(), GSON.fromJson(
                "{\"sceneId\":4,\"pointId\":6,\"rewardId\":220311}",
                TransPointRewardData.class).getId());
    }

    @Test
    void ordinaryWaypointRewardUsesRootFields() {
        var reward = readReward(
                "{\"rewardId\":220311,\"rewardItemList\":[{},{},{}],\"playerExp\":50,\"hcoin\":5}");
        assertEquals(Map.of(102, 50, 201, 5), itemsById(reward));
    }

    @Test
    void specialPointRewardCombinesItemsAndCurrency() {
        var reward = readReward(
                "{\"rewardId\":220312,\"rewardItemList\":[{\"itemId\":101692,\"itemCount\":20},{}],\"hcoin\":5}");
        assertEquals(Map.of(101692, 20, 201, 5), itemsById(reward));
    }

    @Test
    void moraAndAdventureExpAlsoComeFromRootFields() {
        var reward = readReward(
                "{\"rewardId\":100467,\"rewardItemList\":[{\"itemId\":100011,\"itemCount\":5}],\"scoin\":2500,\"playerExp\":125}");
        assertEquals(Map.of(100011, 5, 202, 2500, 102, 125), itemsById(reward));
    }

    private static RewardData readReward(String json) {
        var reward = GSON.fromJson(json, RewardData.class);
        reward.onLoad();
        return reward;
    }

    private static Map<Integer, Integer> itemsById(RewardData reward) {
        return reward.getAllRewardItems().stream()
                .collect(Collectors.toMap(ItemParamData::getId, ItemParamData::getCount));
    }
}
