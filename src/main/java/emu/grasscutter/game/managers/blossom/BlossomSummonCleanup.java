package emu.grasscutter.game.managers.blossom;

import emu.grasscutter.game.entity.EntityClientGadget;
import emu.grasscutter.game.entity.EntityGadget;
import emu.grasscutter.game.entity.EntityMonster;
import emu.grasscutter.game.entity.GameEntity;
import emu.grasscutter.game.world.Scene;
import emu.grasscutter.net.proto.VisionTypeOuterClass.VisionType;
import java.util.*;
import java.util.function.ToIntFunction;

/** Removes only entities summoned by monsters belonging to a completed ley-line fight. */
final class BlossomSummonCleanup {
    private BlossomSummonCleanup() {}

    /**
     * Find the transitive children of encounter roots, even when a child appears before its parent
     * in the scene collection. Never return the roots themselves or unrelated scene entities.
     */
    static <T> List<T> descendants(
            Collection<T> entities,
            Collection<Integer> rootIds,
            ToIntFunction<T> entityId,
            ToIntFunction<T> parentId) {
        Set<Integer> ownedIds = new HashSet<>(rootIds);
        List<T> pending = new ArrayList<>(entities);
        List<T> owned = new ArrayList<>();
        boolean changed;
        do {
            changed = false;
            var iterator = pending.iterator();
            while (iterator.hasNext()) {
                T entity = iterator.next();
                int id = entityId.applyAsInt(entity);
                int parent = parentId.applyAsInt(entity);
                if (parent == 0 || ownedIds.contains(id) || !ownedIds.contains(parent)) continue;
                ownedIds.add(id);
                owned.add(entity);
                iterator.remove();
                changed = true;
            }
        } while (changed);
        return owned;
    }

    private static int parentId(GameEntity entity) {
        if (entity instanceof EntityMonster monster) return monster.getOwnerEntityId();
        if (entity instanceof EntityClientGadget gadget) return gadget.getOwnerEntityId();
        if (entity instanceof EntityGadget gadget && gadget.getOwner() != null) {
            return gadget.getOwner().getId();
        }
        return 0;
    }

    static int remove(Scene scene, Set<Integer> encounterMonsterIds) {
        var spawned = descendants(
                new ArrayList<>(scene.getEntities().values()),
                encounterMonsterIds,
                GameEntity::getId,
                BlossomSummonCleanup::parentId);
        if (!spawned.isEmpty()) {
            // No kill rewards, quest progress or death callbacks for temporary ability summons.
            scene.removeEntities(spawned, VisionType.VisionType_VISION_REMOVE);
        }
        return spawned.size();
    }
}
