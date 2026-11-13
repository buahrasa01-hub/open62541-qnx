/* test_encryption.c — on-target test: encrypted OPC UA round-trip.
 *
 * Generates two self-signed certificates in-process (UA_CreateCertificate,
 * RSA-2048, 14 days), starts a secure-only server (Basic256Sha256,
 * SignAndEncrypt, port 4841) in a worker thread, and connects from the main
 * thread with a client configured for Basic256Sha256 + SignAndEncrypt
 * (direct endpoint, server cert as trust anchor). Reads TestValue = 42 back.
 *
 * The server offers NO 'None' endpoint (setDefaultWithSecureSecurityPolicies),
 * so a successful connect proves the encrypted channel was actually used —
 * a client falling back to None cannot connect at all.
 *
 * Prints PASS/FAIL; exit 0 only if all pass.
 *
 * Build (QNX target): needs the UA_ENABLE_ENCRYPTION=OPENSSL build of the
 * library (see scripts/build-encryption.sh) and -lcrypto -lssl -lsocket -lm.
 * The target image already ships libcrypto.so.3 / libssl.so.3 in the IFS.
 */
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/securitypolicy.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define POLICY_BASIC256SHA256 \
    "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256"

static volatile int serverStop = 0;
static volatile int serverReady = 0;

/* File-scope ByteStrings are zero-initialized, which is UA_BYTESTRING_NULL. */
static UA_ByteString serverCert;
static UA_ByteString serverKey;
static UA_ByteString clientCert;
static UA_ByteString clientKey;

static UA_StatusCode
makeCert(const char *cn, const char *uri,
         UA_ByteString *outKey, UA_ByteString *outCert) {
    UA_String subject[3] = {UA_STRING_STATIC("C=ID"),
                            UA_STRING_STATIC("O=QNX Hackathon"),
                            UA_STRING_STATIC("")};
    /* UA_STRING_STATIC needs a compile-time constant; build CN at runtime */
    char cnBuf[64];
    snprintf(cnBuf, sizeof(cnBuf), "CN=%s", cn);
    subject[2] = UA_String_fromChars(cnBuf);

    UA_String san[2] = {UA_STRING_STATIC("DNS:localhost"),
                        UA_STRING_STATIC("")};
    char uriBuf[96];
    snprintf(uriBuf, sizeof(uriBuf), "URI:%s", uri);
    san[1] = UA_String_fromChars(uriBuf);

    UA_KeyValueMap *kvm = UA_KeyValueMap_new();
    UA_UInt16 expiresIn = 14;
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "expires-in-days"),
                             (void *)&expiresIn, &UA_TYPES[UA_TYPES_UINT16]);
    UA_UInt32 keyBits = 2048;
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "key-size-bits"),
                             (void *)&keyBits, &UA_TYPES[UA_TYPES_UINT32]);

    UA_StatusCode res = UA_CreateCertificate(
        UA_Log_Stdout, subject, 3, san, 2, UA_CERTIFICATEFORMAT_DER, kvm,
        outKey, outCert);
    UA_KeyValueMap_delete(kvm);
    UA_String_clear(&subject[2]);
    UA_String_clear(&san[1]);
    return res;
}

static void *
serverThread(void *arg) {
    (void)arg;
    UA_Server *server = UA_Server_new();
    if(!server)
        return NULL;
    UA_ServerConfig *config = UA_Server_getConfig(server);

    UA_StatusCode res =
        UA_ServerConfig_setDefaultWithSecureSecurityPolicies(
            config, 4841, &serverCert, &serverKey,
            &clientCert, 1, NULL, 0, NULL, 0);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: server secure config (0x%08x)\n", (unsigned)res);
        UA_Server_delete(server);
        return NULL;
    }

    /* The server's ApplicationUri must match the URI in its certificate.
     * NOTE: setDefault*() resets the config first (it re-allocates
     * applicationDescription with the default URI), so this assignment must
     * come AFTER the call above — setting it before is silently wiped. The
     * "does not match the URI" warnings printed while the policies are
     * added are benign: the policies are still offered, and the advertised
     * ApplicationDescription below is what clients see. */
    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri =
        UA_String_fromChars("urn:qnx-sensors:server");

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "TestValue");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.valueRank = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    UA_Int32 initial = 42;
    UA_Variant_setScalar(&attr.value, &initial, &UA_TYPES[UA_TYPES_INT32]);
    res = UA_Server_addVariableNode(
        server, UA_NODEID_NUMERIC(1, 2001),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, "TestValue"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr, NULL, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: server add TestValue (0x%08x)\n", (unsigned)res);
        UA_Server_delete(server);
        return NULL;
    }

    res = UA_Server_run_startup(server);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: server startup (0x%08x)\n", (unsigned)res);
        UA_Server_delete(server);
        return NULL;
    }
    serverReady = 1;
    printf("INFO: secure server listening on port 4841 (Basic256Sha256 only)\n");
    while(!serverStop)
        UA_Server_run_iterate(server, true);
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    return NULL;
}

int
main(void) {
    int failures = 0;

    printf("INFO: generating server certificate (RSA-2048)...\n");
    UA_StatusCode res =
        makeCert("QNX-Sensors-Server", "urn:qnx-sensors:server",
                 &serverKey, &serverCert);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: server certificate generation (0x%08x)\n",
               (unsigned)res);
        return 1;
    }
    printf("PASS: server certificate generated (%zu bytes)\n",
           serverCert.length);
    printf("INFO: generating client certificate (RSA-2048)...\n");
    res = makeCert("QNX-Test-Client", "urn:qnx-sensors:client",
                   &clientKey, &clientCert);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: client certificate generation (0x%08x)\n",
               (unsigned)res);
        return 1;
    }
    printf("PASS: client certificate generated (%zu bytes)\n",
           clientCert.length);

    pthread_t th;
    if(pthread_create(&th, NULL, serverThread, NULL) != 0) {
        printf("FAIL: pthread_create\n");
        return 1;
    }
    int waited = 0;
    while(!serverReady && waited < 30) {
        sleep(1);
        waited++;
    }
    if(!serverReady) {
        printf("FAIL: secure server did not become ready in 30 s\n");
        serverStop = 1;
        pthread_join(th, NULL);
        return 1;
    }

    UA_Client *client = UA_Client_new();
    if(!client) {
        printf("FAIL: UA_Client_new returned NULL\n");
        serverStop = 1;
        pthread_join(th, NULL);
        return 1;
    }
    UA_ClientConfig *cc = UA_Client_getConfig(client);

    /* The client's ApplicationUri must match the URI in its certificate:
     * the server verifies it at CreateSession and rejects the session
     * (BadCertificateInvalid) on a mismatch. Set it before configuring
     * encryption. */
    UA_String_clear(&cc->clientDescription.applicationUri);
    cc->clientDescription.applicationUri =
        UA_String_fromChars("urn:qnx-sensors:client");

    res = UA_ClientConfig_setDefaultEncryption(cc, clientCert, clientKey,
                                               &serverCert, 1, NULL, 0);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: client setDefaultEncryption (0x%08x)\n", (unsigned)res);
        failures++;
    } else {
        /* Require encryption; direct endpoint (the server offers no None
         * endpoint, so discovery over None cannot work). Mirrors the
         * upstream examples/encryption/client_encryption.c pattern. */
        cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        cc->securityPolicyUri =
            UA_String_fromChars(POLICY_BASIC256SHA256);
        cc->endpoint.securityPolicyUri =
            UA_String_fromChars(POLICY_BASIC256SHA256);
        cc->endpoint.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        cc->endpoint.endpointUrl =
            UA_String_fromChars("opc.tcp://127.0.0.1:4841");
        UA_ByteString_copy(&serverCert, &cc->endpoint.serverCertificate);
        cc->endpoint.userIdentityTokensSize = 0;
        cc->endpoint.userIdentityTokens = (UA_UserTokenPolicy *)
            UA_Array_new(1, &UA_TYPES[UA_TYPES_USERTOKENPOLICY]);
        cc->endpoint.userIdentityTokensSize = 1;
        cc->endpoint.userIdentityTokens[0].tokenType =
            UA_USERTOKENTYPE_CERTIFICATE;
        cc->endpoint.userIdentityTokens[0].policyId =
            UA_String_fromChars("open62541-certificate-policy-sign+encrypt#Basic256Sha256");
        cc->endpoint.userIdentityTokens[0].securityPolicyUri =
            UA_String_fromChars(POLICY_BASIC256SHA256);
        cc->endpoint.transportProfileUri =
            UA_String_fromChars("http://opcfoundation.org/UA-Profile/Transport/uatcp-uasc-uabinary");
        UA_ClientConfig_setAuthenticationCert(cc, clientCert, clientKey);

        res = UA_Client_connect(client, "opc.tcp://127.0.0.1:4841");
        if(res != UA_STATUSCODE_GOOD) {
            printf("FAIL: encrypted client connect (0x%08x)\n", (unsigned)res);
            failures++;
        } else {
            printf("PASS: encrypted client connected (Basic256Sha256, SignAndEncrypt)\n");
            UA_Variant value;
            UA_Variant_init(&value);
            res = UA_Client_readValueAttribute(
                client, UA_NODEID_NUMERIC(1, 2001), &value);
            if(res != UA_STATUSCODE_GOOD) {
                printf("FAIL: readValueAttribute over encrypted channel (0x%08x)\n",
                       (unsigned)res);
                failures++;
            } else if(!UA_Variant_hasScalarType(
                          &value, &UA_TYPES[UA_TYPES_INT32])) {
                printf("FAIL: TestValue is not Int32\n");
                failures++;
            } else if(*(UA_Int32 *)value.data != 42) {
                printf("FAIL: TestValue = %d, expected 42\n",
                       *(UA_Int32 *)value.data);
                failures++;
            } else {
                printf("PASS: TestValue read back as 42 over the encrypted channel\n");
            }
            UA_Variant_clear(&value);
            UA_Client_disconnect(client);
        }
    }
    UA_Client_delete(client);

    serverStop = 1;
    pthread_join(th, NULL);
    printf("encryption: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
