package emu.grasscutter.data.excels;

import emu.grasscutter.data.GameResource;
import emu.grasscutter.data.ResourceType;

@ResourceType(name = "TransPointRewardConfigData.json")
public class TransPointRewardData extends GameResource {
    private int sceneId;
    private int pointId;
    private int rewardId;

    @Override
    public int getId() {
        return (sceneId << 16) + pointId;
    }

    public int getRewardId() {
        return rewardId;
    }
}
