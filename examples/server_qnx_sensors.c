/* server_qnx_sensors.c — OPC UA demo server for the QNX 8.0 port.
 *
 * Exposes a simulated production line under Objects/ProductionLine:
 *   ns=1;i=1001  Temperature  (Double, deg C, read-only)
 *   ns=1;i=1002  Vibration    (Double, mm/s, read-only)
 *   ns=1;i=1003  Count        (UInt32, produced parts, read-only)
 *   ns=1;i=1004  Setpoint     (Double, deg C, read-write)
 *   ns=1;i=1005  FanSpeed     (Double, %, read-write — cooling fan)
 *   ns=1;i=1006  Alarm        (Boolean, read-only — over-temperature or
 *                               high-vibration alarm)
 *
 * A 500 ms repeated callback updates the sensor values. Temperature is a
 * state variable that approaches the operator Setpoint with first-order
 * lag (faster with a higher FanSpeed) plus small process noise; Vibration
 * rises with FanSpeed; Alarm trips when Temperature exceeds Setpoint + 3 C
 * or Vibration exceeds 2.5 mm/s. Security: None (UA_ENABLE_ENCRYPTION=OFF
 * build, v1 scope — see the encryption note in the README). Listens on
 * opc.tcp://0.0.0.0:4840.
 *
 * Shutdown is clean: Ctrl-C / SIGTERM stops the server loop via
 * UA_Server_runUntilInterrupt, and the timed mode runs run_shutdown
 * before exit. Usage: server_qnx_sensors [seconds]
 * (0 or omitted = run until Ctrl-C).
 *
 * Build (QNX target): see scripts/build-target.sh
 *   qcc -Vgcc_ntox86_64 -std=c99 -I<install>/include server_qnx_sensors.c \
 *       -L<install>/lib -lopen62541 -lm -o server_qnx_sensors
 */

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NS 1
#define NODE_TEMPERATURE 1001
#define NODE_VIBRATION 1002
#define NODE_COUNT 1003
#define NODE_SETPOINT 1004
#define NODE_FANSPEED 1005
#define NODE_ALARM 1006

static UA_NodeId temperatureId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_TEMPERATURE}};
static UA_NodeId vibrationId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_VIBRATION}};
static UA_NodeId countId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_COUNT}};
static UA_NodeId setpointId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_SETPOINT}};
static UA_NodeId fanSpeedId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_FANSPEED}};
static UA_NodeId alarmId = {1, UA_NODEIDTYPE_NUMERIC, {NODE_ALARM}};

static double simTime = 0.0;
static UA_UInt32 partsCount = 0;
/* Process state: Temperature approaches the Setpoint with first-order lag
 * instead of jumping to a closed-form value, so a setpoint change shows a
 * visible transient (the behaviour a real control loop would show). */
static double temperatureState = 22.0;

static UA_StatusCode
addVariable(UA_Server *server, UA_NodeId nodeId, const char *name,
            const UA_DataType *dataType, UA_Variant initialValue,
            UA_Byte accessLevel) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", name);
    attr.description = UA_LOCALIZEDTEXT("en-US", name);
    attr.dataType = dataType->typeId;
    attr.valueRank = UA_VALUERANK_SCALAR;
    attr.accessLevel = accessLevel;
    attr.value = initialValue;
    return UA_Server_addVariableNode(
        server, nodeId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(NS, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr, NULL, NULL);
}

static void
updateSensors(UA_Server *server, void *data) {
    (void)data;
    UA_Variant value;
    UA_Variant_init(&value);

    /* Read the operator inputs; fall back to defaults if a read fails. */
    UA_Variant spValue;
    UA_Variant_init(&spValue);
    UA_StatusCode res =
        UA_Server_readValue(server, setpointId, &spValue);
    double setpoint = 22.0;
    if(res == UA_STATUSCODE_GOOD &&
       UA_Variant_hasScalarType(&spValue, &UA_TYPES[UA_TYPES_DOUBLE]))
        setpoint = *(UA_Double *)spValue.data;
    UA_Variant_clear(&spValue);

    UA_Variant fsValue;
    UA_Variant_init(&fsValue);
    res = UA_Server_readValue(server, fanSpeedId, &fsValue);
    double fanSpeed = 50.0;
    if(res == UA_STATUSCODE_GOOD &&
       UA_Variant_hasScalarType(&fsValue, &UA_TYPES[UA_TYPES_DOUBLE]))
        fanSpeed = *(UA_Double *)fsValue.data;
    UA_Variant_clear(&fsValue);
    if(fanSpeed < 0.0)
        fanSpeed = 0.0;
    if(fanSpeed > 100.0)
        fanSpeed = 100.0;

    /* First-order lag toward the setpoint; the fan speeds the approach.
     * Small sinusoidal terms are process noise, not the signal itself. */
    simTime += 0.5;
    double rate = 0.05 * (0.5 + fanSpeed / 100.0);
    temperatureState += (setpoint - temperatureState) * rate +
        0.05 * sin(simTime / 1.7);
    double temperature = temperatureState;
    double vibration =
        0.8 + (fanSpeed / 100.0) * 1.2 + 0.3 * sin(simTime / 3.7) +
        0.15 * sin(simTime / 1.1);
    if(vibration < 0.0)
        vibration = -vibration;
    partsCount += 1;

    UA_Boolean alarm = (temperature > setpoint + 3.0) || (vibration > 2.5);

    UA_Variant_setScalar(&value, &temperature, &UA_TYPES[UA_TYPES_DOUBLE]);
    UA_Server_writeValue(server, temperatureId, value);
    UA_Variant_setScalar(&value, &vibration, &UA_TYPES[UA_TYPES_DOUBLE]);
    UA_Server_writeValue(server, vibrationId, value);
    UA_Variant_setScalar(&value, &partsCount, &UA_TYPES[UA_TYPES_UINT32]);
    UA_Server_writeValue(server, countId, value);
    UA_Variant_setScalar(&value, &alarm, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_Server_writeValue(server, alarmId, value);
}

int
main(int argc, char *argv[]) {
    double runSeconds = 0.0; /* 0 = run until interrupt */
    if(argc > 1)
        runSeconds = atof(argv[1]);

    UA_Server *server = UA_Server_new();
    if(!server) {
        fprintf(stderr, "FATAL: UA_Server_new failed\n");
        return 1;
    }

    /* ProductionLine object */
    UA_ObjectAttributes objAttr = UA_ObjectAttributes_default;
    objAttr.displayName = UA_LOCALIZEDTEXT("en-US", "ProductionLine");
    UA_StatusCode res = UA_Server_addObjectNode(
        server, UA_NODEID_NUMERIC(NS, 1000),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(NS, "ProductionLine"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        objAttr, NULL, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: addObjectNode failed 0x%08x\n", res);
        return 1;
    }

    UA_Variant v;
    double d = 22.0;
    UA_Variant_init(&v);
    UA_Variant_setScalar(&v, &d, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = addVariable(server, temperatureId, "Temperature",
                      &UA_TYPES[UA_TYPES_DOUBLE], v,
                      UA_ACCESSLEVELMASK_READ);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add Temperature failed 0x%08x\n", res);
        return 1;
    }

    d = 1.1;
    UA_Variant_setScalar(&v, &d, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = addVariable(server, vibrationId, "Vibration",
                      &UA_TYPES[UA_TYPES_DOUBLE], v,
                      UA_ACCESSLEVELMASK_READ);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add Vibration failed 0x%08x\n", res);
        return 1;
    }

    UA_UInt32 u = 0;
    UA_Variant_setScalar(&v, &u, &UA_TYPES[UA_TYPES_UINT32]);
    res = addVariable(server, countId, "Count",
                      &UA_TYPES[UA_TYPES_UINT32], v,
                      UA_ACCESSLEVELMASK_READ);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add Count failed 0x%08x\n", res);
        return 1;
    }

    d = 22.0;
    UA_Variant_setScalar(&v, &d, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = addVariable(server, setpointId, "Setpoint",
                      &UA_TYPES[UA_TYPES_DOUBLE], v,
                      UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add Setpoint failed 0x%08x\n", res);
        return 1;
    }

    d = 50.0;
    UA_Variant_setScalar(&v, &d, &UA_TYPES[UA_TYPES_DOUBLE]);
    res = addVariable(server, fanSpeedId, "FanSpeed",
                      &UA_TYPES[UA_TYPES_DOUBLE], v,
                      UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add FanSpeed failed 0x%08x\n", res);
        return 1;
    }

    UA_Boolean b = false;
    UA_Variant_setScalar(&v, &b, &UA_TYPES[UA_TYPES_BOOLEAN]);
    res = addVariable(server, alarmId, "Alarm",
                      &UA_TYPES[UA_TYPES_BOOLEAN], v,
                      UA_ACCESSLEVELMASK_READ);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: add Alarm failed 0x%08x\n", res);
        return 1;
    }

    UA_UInt64 callbackId = 0;
    res = UA_Server_addRepeatedCallback(server, updateSensors, NULL,
                                        500.0, &callbackId);
    if(res != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "FATAL: addRepeatedCallback failed 0x%08x\n", res);
        return 1;
    }

    printf("QNX-SENSORS-READY opc.tcp://0.0.0.0:4840 ns=1;i=1001..1006\n");
    fflush(stdout);

    if(runSeconds <= 0.0) {
        res = UA_Server_runUntilInterrupt(server);
    } else {
        /* NOTE: UA_Server_run_iterate returns the milliseconds until the
         * next scheduled callback (UA_UInt16) -- NOT a UA_StatusCode.
         * Treating a non-zero return as an error kills the server on the
         * first iteration. The upstream pattern is to ignore the return
         * value and loop until the deadline. */
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        res = UA_Server_run_startup(server);
        if(res == UA_STATUSCODE_GOOD) {
            for(;;) {
                (void)UA_Server_run_iterate(server, true);
                clock_gettime(CLOCK_MONOTONIC, &now);
                double elapsed = (double)(now.tv_sec - start.tv_sec) +
                    (double)(now.tv_nsec - start.tv_nsec) / 1e9;
                if(elapsed >= runSeconds)
                    break;
            }
            res = UA_Server_run_shutdown(server);
        }
        printf("QNX-SENSORS-DONE requested %.1fs, parts=%u\n", runSeconds,
               partsCount);
    }

    UA_Server_delete(server);
    return (res == UA_STATUSCODE_GOOD) ? 0 : 1;
}
