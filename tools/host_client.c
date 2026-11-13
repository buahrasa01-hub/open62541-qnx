/* host_client.c — host-side OPC UA client for the QNX demo server.
 *
 * Connects to the demo server (default opc.tcp://127.0.0.1:4840 — the QEMU
 * guest port is forwarded to the host, see run-qemu-slirp.sh), then:
 *   1. reads Temperature / Vibration / Count / Setpoint / FanSpeed / Alarm
 *      (ns=1;i=1001..1006)
 *   2. writes Setpoint = 24.0 and FanSpeed = 75.0, reads both back
 *   3. subscribes to Temperature + Count and waits for >= 2 data-change
 *      notifications on each (server updates every 500 ms)
 *   4. checks Alarm is false in the normal regime (Temperature near the
 *      Setpoint, Vibration well under 2.5 mm/s at FanSpeed 75)
 *
 * Prints PASS/FAIL lines; exit 0 only if every step passes.
 *
 * Build (host, native gcc, against the amalgamation from scripts/build.sh):
 *   see scripts/build-host-client.sh
 *   gcc -std=c99 -I<build-dir> host_client.c <build-dir>/open62541.c \
 *       -lm -lpthread -o host_client
 * NOTE: this file includes the single amalgamated header "open62541.h"
 * (the standard single-file usage), not the modular open62541/*.h tree.
 */
#include "open62541.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int tempNotifications = 0;
static int countNotifications = 0;
static double lastTemperature = 0.0;
static UA_UInt32 lastCount = 0;

static void
dataChangeCallback(UA_Client *client, UA_UInt32 subId, void *subContext,
                   UA_UInt32 monId, void *monContext, UA_DataValue *value) {
    (void)client;
    (void)subId;
    (void)subContext;
    (void)monId;
    /* monContext identifies the node: 1 = Temperature, 2 = Count (set at
     * monitored-item creation; server-assigned monId is not stable). */
    uintptr_t which = (uintptr_t)monContext;
    if(!value->hasValue)
        return;
    if(which == 1 && UA_Variant_hasScalarType(&value->value,
                                              &UA_TYPES[UA_TYPES_DOUBLE])) {
        tempNotifications++;
        lastTemperature = *(UA_Double *)value->value.data;
        printf("NOTIFY Temperature=%.2f\n", lastTemperature);
    } else if(which == 2 &&
              UA_Variant_hasScalarType(&value->value,
                                       &UA_TYPES[UA_TYPES_UINT32])) {
        countNotifications++;
        lastCount = *(UA_UInt32 *)value->value.data;
        printf("NOTIFY Count=%u\n", lastCount);
    }
}

static int
readDouble(UA_Client *client, UA_UInt32 nodeIdNum, const char *name,
           double *out) {
    UA_Variant value;
    UA_Variant_init(&value);
    UA_StatusCode res = UA_Client_readValueAttribute(
        client, UA_NODEID_NUMERIC(1, nodeIdNum), &value);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: read %s (0x%08x)\n", name, (unsigned)res);
        return 1;
    }
    if(!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        printf("FAIL: %s is not Double\n", name);
        UA_Variant_clear(&value);
        return 1;
    }
    *out = *(UA_Double *)value.data;
    printf("READ %s=%.2f\n", name, *out);
    UA_Variant_clear(&value);
    return 0;
}

static int
readUInt32(UA_Client *client, UA_UInt32 nodeIdNum, const char *name,
           UA_UInt32 *out) {
    UA_Variant value;
    UA_Variant_init(&value);
    UA_StatusCode res = UA_Client_readValueAttribute(
        client, UA_NODEID_NUMERIC(1, nodeIdNum), &value);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: read %s (0x%08x)\n", name, (unsigned)res);
        return 1;
    }
    if(!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
        printf("FAIL: %s is not UInt32\n", name);
        UA_Variant_clear(&value);
        return 1;
    }
    *out = *(UA_UInt32 *)value.data;
    printf("READ %s=%u\n", name, *out);
    UA_Variant_clear(&value);
    return 0;
}

static int
readBoolean(UA_Client *client, UA_UInt32 nodeIdNum, const char *name,
            UA_Boolean *out) {
    UA_Variant value;
    UA_Variant_init(&value);
    UA_StatusCode res = UA_Client_readValueAttribute(
        client, UA_NODEID_NUMERIC(1, nodeIdNum), &value);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: read %s (0x%08x)\n", name, (unsigned)res);
        return 1;
    }
    if(!UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        printf("FAIL: %s is not Boolean\n", name);
        UA_Variant_clear(&value);
        return 1;
    }
    *out = *(UA_Boolean *)value.data;
    printf("READ %s=%s\n", name, *out ? "true" : "false");
    UA_Variant_clear(&value);
    return 0;
}

int
main(int argc, char *argv[]) {
    const char *endpoint =
        argc > 1 ? argv[1] : "opc.tcp://127.0.0.1:4840";
    int failures = 0;

    UA_Client *client = UA_Client_new();
    if(!client) {
        printf("FAIL: UA_Client_new returned NULL\n");
        return 1;
    }
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    UA_StatusCode res = UA_Client_connect(client, endpoint);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: connect %s (0x%08x)\n", endpoint, (unsigned)res);
        UA_Client_delete(client);
        return 1;
    }
    printf("PASS: connected to %s\n", endpoint);

    /* 1. Read all six nodes */
    double temperature = 0.0, vibration = 0.0, setpoint = 0.0, fanSpeed = 0.0;
    UA_UInt32 count = 0;
    UA_Boolean alarm = false;
    failures += readDouble(client, 1001, "Temperature", &temperature);
    failures += readDouble(client, 1002, "Vibration", &vibration);
    failures += readUInt32(client, 1003, "Count", &count);
    failures += readDouble(client, 1004, "Setpoint", &setpoint);
    failures += readDouble(client, 1005, "FanSpeed", &fanSpeed);
    failures += readBoolean(client, 1006, "Alarm", &alarm);
    if(failures == 0)
        printf("PASS: all six nodes readable\n");
    if(temperature < 10.0 || temperature > 40.0) {
        printf("FAIL: Temperature %.2f outside sane range [10,40]\n",
               temperature);
        failures++;
    }
    if(vibration < 0.0 || vibration > 10.0) {
        printf("FAIL: Vibration %.2f outside sane range [0,10]\n", vibration);
        failures++;
    }
    if(fanSpeed < 0.0 || fanSpeed > 100.0) {
        printf("FAIL: FanSpeed %.2f outside sane range [0,100]\n", fanSpeed);
        failures++;
    }
    if(alarm) {
        printf("FAIL: Alarm is true in the normal regime\n");
        failures++;
    }

    /* 2. Write Setpoint = 24.0, read it back */
    UA_Variant wv;
    UA_Variant_init(&wv);
    double newSetpoint = 24.0;
    UA_Variant_setScalar(&wv, &newSetpoint, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = UA_Client_writeValueAttribute(client, UA_NODEID_NUMERIC(1, 1004),
                                        &wv);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: write Setpoint (0x%08x)\n", (unsigned)res);
        failures++;
    } else {
        double back = 0.0;
        failures += readDouble(client, 1004, "Setpoint-after-write", &back);
        if(back != 24.0) {
            printf("FAIL: Setpoint read back %.2f, expected 24.0\n", back);
            failures++;
        } else {
            printf("PASS: Setpoint write/read-back 24.0\n");
        }
    }

    /* 2b. Write FanSpeed = 75.0, read it back */
    UA_Variant_init(&wv);
    double newFanSpeed = 75.0;
    UA_Variant_setScalar(&wv, &newFanSpeed, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = UA_Client_writeValueAttribute(client, UA_NODEID_NUMERIC(1, 1005),
                                        &wv);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: write FanSpeed (0x%08x)\n", (unsigned)res);
        failures++;
    } else {
        double back = 0.0;
        failures += readDouble(client, 1005, "FanSpeed-after-write", &back);
        if(back != 75.0) {
            printf("FAIL: FanSpeed read back %.2f, expected 75.0\n", back);
            failures++;
        } else {
            printf("PASS: FanSpeed write/read-back 75.0\n");
        }
    }

    /* 3. Subscribe to Temperature + Count, collect notifications */
    UA_CreateSubscriptionResponse subResp = UA_Client_Subscriptions_create(
        client, UA_CreateSubscriptionRequest_default(), NULL, NULL, NULL);
    if(subResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        printf("FAIL: create subscription (0x%08x)\n",
               (unsigned)subResp.responseHeader.serviceResult);
        failures++;
    } else {
        UA_UInt32 subId = subResp.subscriptionId;
        printf("PASS: subscription created (id=%u)\n", subId);
        UA_MonitoredItemCreateResult monTemp =
            UA_Client_MonitoredItems_createDataChange(
                client, subId, UA_TIMESTAMPSTORETURN_BOTH,
                UA_MonitoredItemCreateRequest_default(
                    UA_NODEID_NUMERIC(1, 1001)),
                (void *)(uintptr_t)1, dataChangeCallback, NULL);
        UA_MonitoredItemCreateResult monCount =
            UA_Client_MonitoredItems_createDataChange(
                client, subId, UA_TIMESTAMPSTORETURN_BOTH,
                UA_MonitoredItemCreateRequest_default(
                    UA_NODEID_NUMERIC(1, 1003)),
                (void *)(uintptr_t)2, dataChangeCallback, NULL);
        if(monTemp.statusCode != UA_STATUSCODE_GOOD ||
           monCount.statusCode != UA_STATUSCODE_GOOD) {
            printf("FAIL: create monitored items (0x%08x / 0x%08x)\n",
                   (unsigned)monTemp.statusCode,
                   (unsigned)monCount.statusCode);
            failures++;
        } else {
            printf("PASS: monitored items created (ids %u, %u)\n",
                   monTemp.monitoredItemId, monCount.monitoredItemId);
            struct timespec start, now;
            clock_gettime(CLOCK_MONOTONIC, &start);
            do {
                UA_Client_run_iterate(client, 100);
                clock_gettime(CLOCK_MONOTONIC, &now);
            } while(((double)(now.tv_sec - start.tv_sec) +
                     (double)(now.tv_nsec - start.tv_nsec) / 1e9) < 5.0);
            printf("INFO: Temperature notifications=%d, Count=%d\n",
                   tempNotifications, countNotifications);
            if(tempNotifications < 2) {
                printf("FAIL: Temperature notifications %d < 2\n",
                       tempNotifications);
                failures++;
            } else {
                printf("PASS: Temperature notifications >= 2\n");
            }
            if(countNotifications < 2) {
                printf("FAIL: Count notifications %d < 2\n",
                       countNotifications);
                failures++;
            } else {
                printf("PASS: Count notifications >= 2\n");
            }
        }
    }

    UA_Client_disconnect(client);
    UA_Client_delete(client);
    printf("host-client: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
