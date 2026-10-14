# Handbook claim payload validation

The existing chapter and target claim handlers manually read a uint32 ID from protobuf wire data. They must finish validating the payload before invoking the existing reward helper. Returning immediately after the first ID allowed malformed trailing bytes to reach reward delivery.

The parsers now:

- Require varint wire type for every occurrence of the selected ID field.
- Reject malformed prefixes and suffixes, truncated fields, invalid tags, and unexpected or mismatched end-groups.
- Preserve valid unknown fields, including matched unknown groups.
- Use the last scalar ID occurrence, including zero, consistently with protobuf scalar parsing.
- Return the existing zero-ID rejection sentinel on invalid input, without retaining an earlier valid ID.

This change does not modify reward eligibility, delivery, persistence, response construction, packet registration, opcodes, or schemas. It validates the field selections already used by these handlers; it does not establish new protocol semantics.

## Regression evidence

`InvestigationRewardPayloadValidationTest` invokes both actual handlers with an earned reward and synthetic resource records inside an isolated test JVM. It records actual inventory delivery, progress state, save calls, and decoded responses. It never accesses real accounts or starts a server.

There are 50 cases: 36 malformed payloads, 12 valid payloads, and 2 duplicate-ID requests whose final value is zero. Against `origin/main` at `4ae9884d`, 32 cases fail; after the parser fix, all 50 pass. Valid cases verify that rewards still reach inventory and persist normally.

Both handlers are invoked directly. In particular, chapter request identity in the 7.1 client remains unresolved: these tests prove the existing local parser defect, not that a real client can reach the chapter handler. Existing registration is left untouched; this change neither enables an unmapped claim nor guesses a replacement opcode, schema, or field meaning.
