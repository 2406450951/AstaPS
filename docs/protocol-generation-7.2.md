# 7.2 protocol

The protocol lives in the source tree:

- `src/main/proto/`: one `.proto` per type (types that refer to each other recursively share a file).
- `src/generated/java/`: the Java protoc generates from them, checked in and compiled as its own source set with `-Xlint:all -Werror`.

After editing `src/main/proto`, run `./gradlew generateProtocolJava` and commit both directories. The Protocol generation workflow regenerates the Java and fails if it differs from the checked-in copy.

## Where the 7.2 sources came from

The server's 7.1 protocol (base descriptors, `compat` corrections and TPS renames) was renumbered to 7.2 with `tools/protocol/remap/remap_71_to_72.py`, keeping every name and type the server code uses. The script bridges through the kitkat 7.1.0 dump to the 7.2 NT dump:

1. A server message is matched to its 7.1 dump message by its 7.1 CmdId, then by name, then through a parent field of the same 7.1 number.
2. A 7.1 dump message is matched to its 7.2 message by name, then through a parent field of the same name.
3. Each server field takes the 7.1 dump name found at its 7.1 number, and the 7.2 number of that name. Names are compared ignoring case and underscores. In messages whose named fields all kept their numbers, obfuscated fields keep theirs too.
4. Fields with no 7.2 number are parked at 20000 and up, so they are not read or written.

`WindSeedType1Notify` is mapped explicitly to `_PlayerNormalLuaShellNotify` (the UID watermark): CmdId 21002, payload field 15.

`tools/protocol/remap/export_proto_72.py` then wrote `src/main/proto`:

- Every server type with a readable name is kept, plus the obfuscated types those, or the Java code, refer to.
- Named NT messages the server does not use are left out, to keep the jar small. Passing `--with-nt-additions` adds them: references to types the server already has use the server's name, and NT enums (`message X { enum Value }`) become plain enums whose values are prefixed with the enum name.

Rerunning both scripts needs the 7.1 server descriptor set, built from a commit that still has `protocol/7.1`, plus the kitkat 7.1.0 and NT 7.2 dumps:

```
python3 -I tools/protocol/remap/remap_71_to_72.py <7.1 server.desc> <kitkat Deobfuscated.proto> <NT .proto> \
    src/main/java/emu/grasscutter/net/packet/PacketOpcodes.java out72.desc report.json
python3 -I tools/protocol/remap/export_proto_72.py out72.desc <7.1 server.desc> <kitkat Deobfuscated.proto> \
    <NT .proto> src/main/java/emu/grasscutter/net/packet/PacketOpcodes.java <java-refs.txt> src/main/proto \
    [--with-nt-additions]
```

`tools/protocol/remap/recover_lost_72.py` then recovered parked fields across messages: both dumps give a field the same name in every message, so a 7.1 name and a 7.2 name that are left unmatched together in at least two mapped messages, and only there, with matching types, are the same field. That recovered 183 fields. Its `--single` rule (one unmatched field of a type on each side of a single message) is weaker and was not applied.

Fixes made by hand after export, such as `GachaInfo`, are commented in the `.proto` files themselves. Rerunning the exporter would overwrite them.

What is still missing is listed in `protocol-7.2-gaps.md`.
