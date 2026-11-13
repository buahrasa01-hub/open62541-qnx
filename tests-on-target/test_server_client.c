/* test_server_client.c — on-target test: server + client round-trip.
 *
 * Starts an OPC UA server in a worker thread (one variable TestValue = 42,
 * ns=1;i=2001, port 4840) and connects to it from the main thread with a
 * real UA_Client over the loopback interface: connect, read the value back,
 * verify it. This is the on-target equivalent of the upstream client/server
 * tests, without the libcheck framework.
 *
 * Prints PASS/FAIL; exit 0 only if all pass.
 *
 * Build (QNX target): see scripts/build-target.sh (needs -pthread)
 */
#include <open62541/client.h>
#include <open62541/client_highlevel.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static volatile int serverStop = 0;

static void *
serverThread(void *arg) {
    (void)arg;
    UA_Server *server = UA_Server_new();
    if(!server)
        return NULL;

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "TestValue");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.valueRank = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    UA_Int32 initial = 42;
    UA_Variant_setScalar(&attr.value, &initial, &UA_TYPES[UA_TYPES_INT32]);
    UA_StatusCode res = UA_Server_addVariableNode(
        server, UA_NODEID_NUMERIC(1, 2001),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, "TestValue"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr, NULL, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return NULL;
    }

    res = UA_Server_run_startup(server);
    if(res != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return NULL;
    }
    while(!serverStop)
        UA_Server_run_iterate(server, true);
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    return NULL;
}

int
main(void) {
    int failures = 0;
    pthread_t th;
    if(pthread_create(&th, NULL, serverThread, NULL) != 0) {
        printf("FAIL: pthread_create\n");
        return 1;
    }
    sleep(2); /* let the server bind port 4840 */

    UA_Client *client = UA_Client_new();
    if(!client) {
        printf("FAIL: UA_Client_new returned NULL\n");
        serverStop = 1;
        pthread_join(th, NULL);
        return 1;
    }
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    UA_StatusCode res =
        UA_Client_connect(client, "opc.tcp://127.0.0.1:4840");
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: client connect (0x%08x)\n", (unsigned)res);
        failures++;
    } else {
        printf("PASS: client connected to opc.tcp://127.0.0.1:4840\n");
        UA_Variant value;
        UA_Variant_init(&value);
        res = UA_Client_readValueAttribute(
            client, UA_NODEID_NUMERIC(1, 2001), &value);
        if(res != UA_STATUSCODE_GOOD) {
            printf("FAIL: readValueAttribute (0x%08x)\n", (unsigned)res);
            failures++;
        } else if(!UA_Variant_hasScalarType(&value,
                                            &UA_TYPES[UA_TYPES_INT32])) {
            printf("FAIL: TestValue is not Int32\n");
            failures++;
        } else if(*(UA_Int32 *)value.data != 42) {
            printf("FAIL: TestValue = %d, expected 42\n",
                   *(UA_Int32 *)value.data);
            failures++;
        } else {
            printf("PASS: TestValue read back as 42\n");
        }
        UA_Variant_clear(&value);
        UA_Client_disconnect(client);
    }
    UA_Client_delete(client);

    serverStop = 1;
    pthread_join(th, NULL);
    printf("server-client: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
