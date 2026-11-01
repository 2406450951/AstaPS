package emu.grasscutter.game.reward;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import emu.grasscutter.data.ResourceRegistry;
import emu.grasscutter.game.drop.YamlLootTables;
import java.util.Map;
import java.util.Random;
import org.junit.jupiter.api.Test;

class BlossomCssAuthorityTest {
    private static RewardConfiguration rewards(String stylesheet) {
        var registry = new ResourceRegistry(Map.of(
                "genshin.experience.adventure", 102,
                "genshin.currency.mora", 202));
        var loot = YamlLootTables.parse("{}", registry::resolve);
        return new RewardConfiguration(loot, CssRewards.parse(stylesheet, registry, loot));
    }

    @Test
    void configuredBlossomKindUsesLootCssForItsRewards() {
        var config = rewards("""
                blossom { genshin.experience.adventure: 100; }
                blossom[type="custom"] { genshin.currency.mora: 37; }
                """);
        assertEquals(Map.of(102, 100, 202, 37),
                config.rollBlossom("custom", 5, 1, new Random(1)));
        assertEquals(Map.of(102, 200, 202, 74),
                config.rollBlossom("custom", 5, 2, new Random(1)));
    }

    @Test
    void missingKindSpecificRewardsCannotPayOnlyCommonExperience() {
        var config = rewards("blossom { genshin.experience.adventure: 100; }");
        assertThrows(IllegalStateException.class,
                () -> config.rollBlossom("custom", 5, 1, new Random(1)));
    }
}