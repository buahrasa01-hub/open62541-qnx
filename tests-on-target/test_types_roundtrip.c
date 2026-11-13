/* test_types_roundtrip.c — on-target test: binary encode/decode round-trips.
 *
 * Mirrors the coverage of upstream check_types_builtin.c (which needs the
 * libcheck framework — not available on QNX 8.0) using only the public API:
 * UA_encodeBinary / UA_decodeBinary on UA_Variant values, plus NodeId
 * equality. Prints PASS/FAIL per case; exit 0 only if all pass.
 *
 * Build (QNX target): see scripts/build-target.sh
 */
#include <open62541/types.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if(!(cond)) {                                                        \
            printf("FAIL: %s\n", msg);                                       \
            failures++;                                                      \
        } else {                                                             \
            printf("PASS: %s\n", msg);                                       \
        }                                                                    \
    } while(0)

static void
roundtripVariant(UA_Variant *src, const char *name) {
    UA_ByteString buf;
    UA_ByteString_init(&buf);
    UA_StatusCode res =
        UA_encodeBinary(src, &UA_TYPES[UA_TYPES_VARIANT], &buf, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: encode %s (0x%08x)\n", name, (unsigned)res);
        failures++;
        return;
    }
    UA_Variant decoded;
    UA_Variant_init(&decoded);
    res = UA_decodeBinary(&buf, &decoded, &UA_TYPES[UA_TYPES_VARIANT], NULL);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: decode %s (0x%08x)\n", name, (unsigned)res);
        failures++;
        UA_ByteString_clear(&buf);
        return;
    }
    UA_Boolean eq = UA_equal(src, &decoded, &UA_TYPES[UA_TYPES_VARIANT]);
    CHECK(eq, name);
    UA_Variant_clear(&decoded);
    UA_ByteString_clear(&buf);
}

int
main(void) {
    UA_Variant v;
    UA_Variant_init(&v);

    /* Scalar Int32 */
    UA_Int32 i32 = -123456;
    UA_Variant_setScalar(&v, &i32, &UA_TYPES[UA_TYPES_INT32]);
    roundtripVariant(&v, "variant Int32 -123456");

    /* Scalar Double */
    UA_Double d = 3.141592653589793;
    UA_Variant_setScalar(&v, &d, &UA_TYPES[UA_TYPES_DOUBLE]);
    roundtripVariant(&v, "variant Double pi");

    /* Scalar String */
    UA_String s = UA_STRING("QNX-OPC-UA");
    UA_Variant_setScalar(&v, &s, &UA_TYPES[UA_TYPES_STRING]);
    roundtripVariant(&v, "variant String QNX-OPC-UA");

    /* Scalar Boolean */
    UA_Boolean b = true;
    UA_Variant_setScalar(&v, &b, &UA_TYPES[UA_TYPES_BOOLEAN]);
    roundtripVariant(&v, "variant Boolean true");

    /* Scalar DateTime (now) */
    UA_DateTime dt = UA_DateTime_now();
    UA_Variant_setScalar(&v, &dt, &UA_TYPES[UA_TYPES_DATETIME]);
    roundtripVariant(&v, "variant DateTime now");

    /* Array of UInt32 */
    UA_UInt32 arr[5] = {0, 1, 2, 3, 4294967295u};
    UA_Variant_setArray(&v, arr, 5, &UA_TYPES[UA_TYPES_UINT32]);
    roundtripVariant(&v, "variant UInt32[5]");

    /* Array of Double */
    UA_Double darr[3] = {-1.5, 0.0, 1.5};
    UA_Variant_setArray(&v, darr, 3, &UA_TYPES[UA_TYPES_DOUBLE]);
    roundtripVariant(&v, "variant Double[3]");

    /* NodeId equality (no encoding involved) */
    UA_NodeId n1 = UA_NODEID_NUMERIC(1, 1001);
    UA_NodeId n2 = UA_NODEID_NUMERIC(1, 1001);
    UA_NodeId n3 = UA_NODEID_NUMERIC(1, 1002);
    CHECK(UA_NodeId_equal(&n1, &n2), "NodeId (1,1001) == (1,1001)");
    CHECK(!UA_NodeId_equal(&n1, &n3), "NodeId (1,1001) != (1,1002)");
    CHECK(UA_String_equal(&s, &s), "String self-equal");

    printf("types-roundtrip: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
