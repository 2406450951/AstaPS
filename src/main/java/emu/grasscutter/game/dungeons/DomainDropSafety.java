package emu.grasscutter.game.dungeons;

import emu.grasscutter.game.dungeons.enums.DungeonSubType;
import java.util.List;
import java.util.Set;
import java.util.function.BiFunction;

/** Validation and one-claim commit guard for configured dungeon rewards. */
final class DomainDropSafety {
    private static final int MAX_COUNT_CHOICES = 10_000;
    private static final int MAX_ITEMS_PER_ROLL = 10_000;

    private DomainDropSafety() {}

    enum RewardSource { NATIVE_ROOT, VALIDATED_PROXY, PREVIEW_ONLY, NONE }

    /** Original 7.1 drop roots take precedence over reconstructed server proxies. */
    static RewardSource chooseRewardSource(
            boolean nativeReady, boolean hasProxy, boolean hasRootPointer, boolean hasPreview) {
        if (nativeReady) return RewardSource.NATIVE_ROOT;
        if (hasProxy) return RewardSource.VALIDATED_PROXY;
        if (hasRootPointer) return RewardSource.NONE;
        return hasPreview ? RewardSource.PREVIEW_ONLY : RewardSource.NONE;
    }

    static void validatePool(int dungeonId, List<DungeonDropEntry> entries) {
        validatePool(dungeonId, entries, DungeonSubType.DUNGEON_SUB_NONE);
    }

    static void validatePool(
            int dungeonId, List<DungeonDropEntry> entries, DungeonSubType subType) {
        if (entries == null || entries.isEmpty()) {
            throw invalid(dungeonId, "no reward entries");
        }
        for (int index = 0; index < entries.size(); index++) {
            DungeonDropEntry entry = entries.get(index);
            if (entry == null || entry.getCounts() == null || entry.getCounts().isEmpty()) {
                throw invalid(dungeonId, "entry " + index + " has no counts");
            }
            List<Integer> counts = entry.getCounts();
            Integer start = counts.get(0);
            Integer end = counts.get(counts.size() - 1);
            if (start == null || end == null || start < 0 || end < start
                    || end > MAX_ITEMS_PER_ROLL) {
                throw invalid(dungeonId, "entry " + index + " has invalid count bounds");
            }
            long choices = (long) end - start + 1;
            if (choices > MAX_COUNT_CHOICES) {
                throw invalid(dungeonId, "entry " + index + " has an excessive count range");
            }
            validateWeights(dungeonId, index, "count", entry.getProbabilities(), (int) choices);

            List<Integer> items = entry.getItems();
            if (items == null || items.isEmpty() || items.stream().anyMatch(id -> id == null || id <= 0)) {
                throw invalid(dungeonId, "entry " + index + " has no valid item pool");
            }
            validateWeights(dungeonId, index, "item", entry.getItemProbabilities(), items.size());
            if (subType == DungeonSubType.DUNGEON_SUB_RELIQUARY
                    && items.stream().anyMatch(id -> id >= 114000 && id < 115000)) {
                throw invalid(dungeonId, "reliquary pool contains weapon ascension materials");
            }
        }
    }

    private static void validateWeights(
            int dungeonId, int index, String kind, List<Integer> weights, int choices) {
        if (weights == null || weights.isEmpty()) {
            return; // No explicit weights means uniform selection.
        }
        if (weights.size() != choices) {
            throw invalid(dungeonId, "entry " + index + " has mismatched " + kind + " weights");
        }
        long total = 0;
        for (Integer weight : weights) {
            if (weight == null || weight < 0) {
                throw invalid(dungeonId, "entry " + index + " has negative/null " + kind + " weight");
            }
            total += weight;
        }
        if (total <= 0 || total >= Integer.MAX_VALUE) {
            throw invalid(dungeonId, "entry " + index + " has invalid " + kind + " weight sum");
        }
    }

    private static IllegalArgumentException invalid(int dungeonId, String explanation) {
        return new IllegalArgumentException(
                "DungeonDrop.json dungeon " + dungeonId + ": " + explanation);
    }

    /**
     * Serialize a single claim for this dungeon session. The grant implementation is responsible
     * for validating the complete item batch and applying any inventory constraints.
     *
     * <p>The grant receives a confirmation callback. It must invoke that callback only after
     * a payment succeeds, and before adding any items. This prevents a retry from charging a
     * second time if the subsequent grant throws. It is not a database transaction.
     */
    static <T> boolean commitOnce(
            Object lock,
            Set<Integer> rewarded,
            int uid,
            List<T> rewards,
            BiFunction<List<T>, Runnable, Boolean> attemptGrant) {
        synchronized (lock) {
            if (rewarded == null || rewarded.contains(uid) || rewards == null
                    || rewards.isEmpty()) {
                return false;
            }
            return attemptGrant.apply(rewards, () -> rewarded.add(uid));
        }
    }
}
