package emu.grasscutter.game.managers.blossom;

import static org.junit.jupiter.api.Assertions.*;
import com.google.gson.Gson;
import emu.grasscutter.data.excels.BlossomCampData;
import emu.grasscutter.game.managers.blossom.BlossomGraph.CityGraph;
import java.util.List;
import java.util.Set;
import org.junit.jupiter.api.Test;

class BlossomGraphTest {
    private static final Gson JSON = new Gson();

    private static BlossomCampData camp(int id, int city, int section, int... next) {
        return JSON.fromJson(
                "{\"id\":" + id + ",\"cityId\":" + city +
                ",\"sectionId\":" + section +
                ",\"refreshTypeVec\":[1,3],\"nextCampIdVec\":" +
                JSON.toJson(java.util.Arrays.stream(next).boxed().toList()) + "}",
                BlossomCampData.class);
    }

    @Test
    void successorTraversalHandlesBranchesAndCyclesOnce() {
        var graph = new CityGraph(1, List.of(
                camp(100, 1, 10, 101, 102),
                camp(101, 1, 10, 103),
                camp(102, 1, 10, 103),
                camp(103, 1, 10, 100),
                camp(200, 2, 20, 201)), Set.of(1, 3));
        assertEquals(List.of(100), graph.initialCamps.stream().map(n -> n.campId).toList());
        assertEquals(List.of(101, 102, 103, 100),
                graph.findNextCamps(graph.nodes.get(100).next).stream()
                        .map(n -> n.campId).toList());
        assertFalse(graph.nodes.containsKey(200));
    }

    @Test
    void initialCampsContainOneHeadPerSection() {
        var graph = new CityGraph(1, List.of(
                camp(200, 1, 20),
                camp(101, 1, 10),
                camp(100, 1, 10)), Set.of(1, 3));
        assertEquals(Set.of(100, 200), graph.initialCamps.stream()
                .map(n -> n.campId).collect(java.util.stream.Collectors.toSet()));
    }
}