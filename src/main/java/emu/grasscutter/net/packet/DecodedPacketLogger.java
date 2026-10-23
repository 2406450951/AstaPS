package emu.grasscutter.net.packet;

import static emu.grasscutter.config.Configuration.GAME_INFO;
import static emu.grasscutter.config.Configuration.SERVER;

import com.google.protobuf.Message;
import emu.grasscutter.Grasscutter;
import emu.grasscutter.server.game.GameSession;

import java.lang.reflect.Method;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

public final class DecodedPacketLogger {

    private static final String PROTO_PACKAGE = "emu.grasscutter.net.proto.";

    private static final Map<String, Optional<Method>> PARSER_CACHE = new ConcurrentHashMap<>();

    private DecodedPacketLogger() {}

    /**
     * Logs a decoded protobuf packet when payload logging is enabled.
     *
     * @param session session associated with the packet, if available
     * @param direction RECV or SEND
     * @param opcode packet opcode
     * @param payload serialized protobuf payload
     * @param handler packet handler, if available
     */
    public static void logDecodedPacket(GameSession session, String direction, int opcode,
            byte[] payload, PacketHandler handler) {

        if (payload == null || !shouldDump(session, opcode)) {
            return;
        }

        Optional<Method> parser = findParser(opcode, handler);
        if (parser.isEmpty()) {
            Grasscutter.getLogger().info("{}: No parser found for {} ({})", direction,
                    PacketOpcodesUtils.getOpcodeName(opcode), opcode);
            return;
        }

        try {
            Object result = parser.get().invoke(null, (Object) payload);

            if (result instanceof Message message) {
                Grasscutter.getLogger().info("{} decoded: {} ({}) via {} -> {}", direction,
                        PacketOpcodesUtils.getOpcodeName(opcode), opcode,
                        handler != null ? handler.getClass().getSimpleName() : "unknown",
                        message.getDescriptorForType().getFullName());

                if (GAME_INFO.isShowPacketPayload) {
                    Grasscutter.getLogger().info("{}", message);
                }
            }
        } catch (ReflectiveOperationException e) {
            Grasscutter.getLogger().info("Failed to decode {} packet {} ({})", direction,
                    PacketOpcodesUtils.getOpcodeName(opcode), opcode, e);
        }
    }

    private static Optional<Method> findParser(int opcode, PacketHandler handler) {

        String messageName =
                handler != null ? handler.getClass().getSimpleName().replaceFirst("^Handler", "")
                        : PacketOpcodesUtils.getOpcodeName(opcode);

        if (messageName == null || messageName.isBlank()) {
            return Optional.empty();
        }

        String className = PROTO_PACKAGE + messageName + "OuterClass$" + messageName;

        return PARSER_CACHE.computeIfAbsent(className, ignored -> {
            try {
                Class<?> type = Class.forName(className);

                if (!Message.class.isAssignableFrom(type)) {
                    return Optional.empty();
                }

                return Optional.of(type.getMethod("parseFrom", byte[].class));
            } catch (ReflectiveOperationException e) {
                return Optional.empty();
            }
        });
    }

    private static boolean shouldDump(GameSession session, int opcode) {
        if (PacketOpcodes.BANNED_PACKETS.contains(opcode))
            return false;
        return switch (GAME_INFO.logPackets) {
            case ALL -> !PacketOpcodesUtils.LOOP_PACKETS.contains(opcode)
                    || GAME_INFO.isShowLoopPackets;
            case WHITELIST -> SERVER.debugWhitelist.contains(opcode);
            case BLACKLIST -> !SERVER.debugBlacklist.contains(opcode);
            default -> false;
        };
    }
}
