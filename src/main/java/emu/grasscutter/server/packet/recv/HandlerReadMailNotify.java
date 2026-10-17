package emu.grasscutter.server.packet.recv;

import emu.grasscutter.game.mail.Mail;
import emu.grasscutter.net.packet.Opcodes;
import emu.grasscutter.net.packet.PacketHandler;
import emu.grasscutter.net.packet.PacketOpcodes;
import emu.grasscutter.net.proto.ReadMailNotifyOuterClass.ReadMailNotify;
import emu.grasscutter.server.game.GameSession;
import emu.grasscutter.server.packet.send.PacketMailChangeNotify;

import java.util.ArrayList;
import java.util.List;

@Opcodes(PacketOpcodes.ReadMailNotify)
public class HandlerReadMailNotify extends PacketHandler {
    /*
     * The bundled proto definitions predate the 7.1 client, so the generated
     * accessors can come back empty even when the client did send mail ids.
     * Fall back to a plain protobuf wire-format walk and collect every varint
     * (standalone or packed) as a candidate mail id.
     */
    private static List<Integer> parseMailIdsFromRaw(byte[] payload) {
        List<Integer> ids = new ArrayList<>();
        int i = 0;
        while (payload != null && i < payload.length) {
            int tag = payload[i++] & 0xFF;
            int wire = tag & 0x07;
            if (wire == 0) {
                long value = 0;
                int shift = 0;
                while (i < payload.length) {
                    int b = payload[i++] & 0xFF;
                    value |= (long) (b & 0x7F) << shift;
                    if ((b & 0x80) == 0) break;
                    shift += 7;
                }
                ids.add((int) value);
            } else if (wire == 2) {
                if (i >= payload.length) break;
                int len = payload[i++] & 0xFF;
                int end = Math.min(i + len, payload.length);
                while (i < end) {
                    long value = 0;
                    int shift = 0;
                    while (i < end) {
                        int b = payload[i++] & 0xFF;
                        value |= (long) (b & 0x7F) << shift;
                        if ((b & 0x80) == 0) break;
                        shift += 7;
                    }
                    ids.add((int) value);
                }
                i = end;
            } else if (wire == 5) {
                i += 4;
            } else if (wire == 1) {
                i += 8;
            } else {
                break;
            }
        }
        return ids;
    }


    @Override
    public void handle(
            GameSession session,
            byte[] header,
            byte[] payload)
            throws Exception {

        ReadMailNotify req =
                ReadMailNotify.parseFrom(payload);

        var player =
                session.getPlayer();

        var mailHandler =
                player.getMailHandler();
        List<Integer> clientMailIds =
                new ArrayList<>(
                        req.getMailIdListList());
        if (clientMailIds.isEmpty()) {
            clientMailIds.addAll(
                    parseMailIdsFromRaw(payload));
        }

        List<Mail> updatedMail =
                new ArrayList<>();

        for (int clientMailId : clientMailIds) {

            /*
             * Convert the ID shown to the client into the index used by
             * LunaGC's internal List<Mail>.
             */
            int internalIndex =
                    mailHandler.toInternalMailIndex(
                            clientMailId);

            if (internalIndex < 0) {
                continue;
            }

            Mail message =
                    player.getMail(
                            internalIndex);

            if (message == null) {
                continue;
            }

            /*
             * There is no need to rewrite and save mail that is already
             * marked as read.
             */
            if (message.isRead) {
                continue;
            }

            message.isRead = true;

            /*
             * replaceMailByIndex expects the internal zero-based index.
             * It also saves the modified Mail document.
             */
            if (player.replaceMailByIndex(
                    internalIndex,
                    message)) {

                updatedMail.add(
                        message);
            }
        }

        /*
         * PacketMailChangeNotify's List<Mail> constructor should place
         * these entries in change_mail_list, because they already exist
         * in the client's mailbox.
         */
        if (!updatedMail.isEmpty()) {
            session.send(
                    new PacketMailChangeNotify(
                            player,
                            updatedMail));
        }
    }
}