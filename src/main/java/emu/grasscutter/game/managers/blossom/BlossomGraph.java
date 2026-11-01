package emu.grasscutter.game.managers.blossom;

import emu.grasscutter.data.excels.BlossomCampData;
import java.util.*;

/** The native per-city camp graph, including section starts and directed successors. */
final class BlossomGraph {
    static final class CampNode {
        final int campId;
        final CityGraph graph;
        final List<CampNode> next = new ArrayList<>();
        BlossomManager.Blossom blossom;

        CampNode(int campId, CityGraph graph) {
            this.campId = campId;
            this.graph = graph;
        }
    }

    static final class CityGraph {
        final int cityId;
        final Map<Integer, CampNode> nodes = new HashMap<>();
        final List<CampNode> initialCamps;

        CityGraph(int cityId, Collection<BlossomCampData> authored,
                Collection<Integer> nativeTypes) {
            this.cityId = cityId;
            var sections = new HashMap<Integer, CampNode>();

            for (var data : authored) {
                if (data.getCityId() != cityId) continue;
                var node = new CampNode(data.getId(), this);
                nodes.put(data.getId(), node);

                if (data.getRefreshTypeVec() == null
                        || !data.getRefreshTypeVec().containsAll(nativeTypes)) continue;
                sections.merge(data.getSectionId(), node,
                        (a, b) -> a.campId < b.campId ? a : b);
            }

            for (var data : authored) {
                if (data.getCityId() != cityId) continue;
                var node = nodes.get(data.getId());
                var nextIds = data.getNextCampIdVec();
                if (nextIds == null) continue;
                for (int nextId : nextIds) {
                    var successor = nodes.get(nextId);
                    if (successor != null) node.next.add(successor);
                }
            }
            initialCamps = List.copyOf(sections.values());
        }

        /** Collect reachable successor candidates in breadth-first order. */
        List<CampNode> findNextCamps(Collection<CampNode> starts) {
            var queue = new ArrayDeque<>(starts);
            var visited = new HashSet<CampNode>();
            var candidates = new ArrayList<CampNode>();
            while (!queue.isEmpty()) {
                var node = queue.removeFirst();
                if (!visited.add(node)) continue;
                candidates.add(node);
                queue.addAll(node.next);
            }
            return candidates;
        }
    }
}
