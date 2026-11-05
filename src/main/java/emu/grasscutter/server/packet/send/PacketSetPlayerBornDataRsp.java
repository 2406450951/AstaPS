package emu.grasscutter.server.packet.send;

import com.google.protobuf.CodedOutputStream;
import emu.grasscutter.net.packet.BasePacket;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.SetPlayerBornDataRspOuterClass.SetPlayerBornDataRsp;
import java.io.ByteArrayOutputStream;
import java.io.IOException;

/** SetPlayerBornDataRsp, written by hand so success stays an empty payload. */
public class PacketSetPlayerBornDataRsp extends BasePacket {
    /** 414 in 7.2 (4385 in 7.1). */
    public static final int CMD_ID = PacketOpcodes.SetPlayerBornDataRsp;

    /** 15 in 7.2 (7 in 7.1). */
    private static final int RETCODE_FIELD_NUMBER = SetPlayerBornDataRsp.RETCODE_FIELD_NUMBER;

    /** Success is encoded as an empty payload because protobuf defaults retcode to zero. */
    public PacketSetPlayerBornDataRsp() {
        super(CMD_ID);
    }

    public PacketSetPlayerBornDataRsp(int retcode) {
        this();
        if (retcode != 0) {
            this.setData(encodeRetcode(retcode));
        }
    }

    private static byte[] encodeRetcode(int retcode) {
        try {
            var bytes = new ByteArrayOutputStream();
            var output = CodedOutputStream.newInstance(bytes);
            output.writeInt32(RETCODE_FIELD_NUMBER, retcode);
            output.flush();
            return bytes.toByteArray();
        } catch (IOException e) {
            throw new IllegalStateException("Failed to encode SetPlayerBornDataRsp", e);
        }
    }
}
