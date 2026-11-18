// =============================================================================
// pintest.c - PINTEST: step every crane output and print every input (v5.0)
// =============================================================================
// For ICD Table 43 stage 4 (unpowered signal handshake with every package).
// RUN WITH THE HIGH VOLTAGE, MOTOR AND MAGNET SUPPLIES OFF.
//
// Console (COM12):  PINTEST        run the whole sequence (~40 s)
//                   PINTEST STOP   abort: every module back to its safe state
//
// Each step holds for PINTEST_STEP_MS and prints one line
//   RIG,PINTEST,<n>/<total>,<header pin>,<signal>,<what is set>,<what to measure>
// so the result can be copied into handshake.csv. It only uses the modules'
// own console test commands, so the outputs go through exactly the same code
// as in normal use. Pins are only those in the wiring document (final pinout).
// =============================================================================
#include "pintest.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rig_config.h"
#include "loadcell.h"
#if RIG_ENABLE_DRIVE
#include "drive_control.h"
#endif
#if RIG_ENABLE_TROLLEY
#include "trolley_control.h"
#endif
#if RIG_ENABLE_MAGNET
#include "magnet_control.h"
#endif

#define PINTEST_STEP_MS 2000

typedef enum { MOD_TROLLEY, MOD_DRIVE, MOD_MAGNET } mod_t;

typedef struct {
    mod_t mod;
    const char *sub, *a1, *a2;  // the module's own console command
    const char *pin, *signal, *set, *measure;
} step_t;

static const step_t STEPS[] = {
    // MCTR 1 trolley (IF1.0)
    {MOD_TROLLEY, "PWM", "25", NULL, "J7.9", "LAT_PWM_ENA", "5 kHz 25 %", "scope: 5 kHz, 25 %, 0/3.3 V (DMM ~0.8 V)"},
    {MOD_TROLLEY, "IN", "FWD", NULL, "J4.4/J4.5 (+J4.6/J4.7)", "IN1/IN2 (IN3/IN4)", "IN1 HIGH, IN2 LOW", "J4.4 3.3 V, J4.5 0 V"},
    {MOD_TROLLEY, "IN", "REV", NULL, "J4.4/J4.5 (+J4.6/J4.7)", "IN1/IN2 (IN3/IN4)", "IN1 LOW, IN2 HIGH", "J4.4 0 V, J4.5 3.3 V"},
    {MOD_TROLLEY, "STOP", NULL, NULL, "J7.9 J4.4-J4.7", "trolley", "all off", "0 V"},
    // MCTR 2 hoist brake (IF15.0)
    {MOD_DRIVE, "BRAKE", "RELEASE", NULL, "J4.8", "HOIST_BRAKE", "release", "3.3 V (if HIGH = release)"},
    {MOD_DRIVE, "BRAKE", "CLAMP", NULL, "J4.8", "HOIST_BRAKE", "clamp", "0 V"},
    // EEII 1 drive (IF6.0)
    {MOD_DRIVE, "EN", "1", NULL, "J3.4", "DRV_EN_LOGIC", "enable", "3.3 V"},
    {MOD_DRIVE, "EN", "0", NULL, "J3.4", "DRV_EN_LOGIC", "disable", "0 V"},
    {MOD_DRIVE, "SQUARE", "A", "50", "J7.8", "VFD_SPWM_A", "10 kHz 50 %", "scope on J7.8 only"},
    {MOD_DRIVE, "SQUARE", "B", "50", "J7.7", "VFD_SPWM_B", "10 kHz 50 %", "scope on J7.7 only"},
    {MOD_DRIVE, "SQUARE", "C", "50", "J7.6", "VFD_SPWM_C", "10 kHz 50 %", "scope on J7.6 only"},
    {MOD_DRIVE, "STOP", NULL, NULL, "J7.6-J7.8 J3.4 J4.8", "drive", "all off", "idle level, 0 V, 0 V"},
    // EEII 2 magnet (IF12.0)
    {MOD_MAGNET, "PWM", "50", NULL, "J7.5", "MAG_PWM", "15 kHz 50 %", "scope: 15 kHz, 50 %"},
    {MOD_MAGNET, "LOGIC", "1", NULL, "J3.6", "MAG_LOGIC", "HIGH", "3.3 V"},
    {MOD_MAGNET, "LOGIC", "0", NULL, "J3.6", "MAG_LOGIC", "LOW", "0 V"},
    {MOD_MAGNET, "STOP", NULL, NULL, "J7.5 J3.6", "magnet", "released", "10 % (release duty), 0 V"},
};
#define N_STEPS (int)(sizeof(STEPS) / sizeof(STEPS[0]))

static volatile bool s_running, s_abort;

static void send(mod_t m, const char *sub, const char *a1, const char *a2)
{
    switch (m) {
#if RIG_ENABLE_TROLLEY
    case MOD_TROLLEY: trolley_command(sub, a1, a2); break;
#endif
#if RIG_ENABLE_DRIVE
    case MOD_DRIVE: drive_command(sub, a1, a2); break;
#endif
#if RIG_ENABLE_MAGNET
    case MOD_MAGNET: magnet_command(sub, a1, a2); break;
#endif
    default: printf("RIG,PINTEST,module not built in - step skipped\n"); break;
    }
}

static void all_safe(void)
{
    send(MOD_TROLLEY, "STOP", NULL, NULL);
    send(MOD_DRIVE, "STOP", NULL, NULL);
    send(MOD_MAGNET, "STOP", NULL, NULL);
}

static void print_inputs(void)
{
    printf("RIG,PINTEST,INPUTS,J3.5 DRV_FAULT_IN = in_fault_j3_5 | J7.12 ENC_A = counts | J6.8 MAG_CURR_IN = sensor_v | "
           "J6.5/J6.6 load cell = LOAD | J6.7 VFD_CURR_IN: not read (open, EEII 1)\n");
#if RIG_ENABLE_DRIVE
    drive_print("PINTEST,DRIVE");
#endif
#if RIG_ENABLE_TROLLEY
    trolley_print("PINTEST,TROLLEY");
#endif
#if RIG_ENABLE_MAGNET
    magnet_print("PINTEST,MAGNET");
#endif
    loadcell_print("PINTEST,LOAD");
}

static void pintest_task(void *arg)
{
    (void)arg;
    printf("RIG,PINTEST,START,%d steps x %d ms - HV, motor and magnet supplies must be OFF. "
           "Dashboard commands are ignored until the end. PINTEST STOP aborts.\n",
           N_STEPS, PINTEST_STEP_MS);
    print_inputs();
    int i = 0;
    for (; i < N_STEPS && !s_abort; i++) {
        const step_t *st = &STEPS[i];
        send(st->mod, st->sub, st->a1, st->a2);
        printf("RIG,PINTEST,%d/%d,%s,%s,%s,%s\n", i + 1, N_STEPS, st->pin, st->signal, st->set, st->measure);
        for (int w = 0; w < PINTEST_STEP_MS / 100 && !s_abort; w++) vTaskDelay(pdMS_TO_TICKS(100));
    }
    all_safe();
    vTaskDelay(pdMS_TO_TICKS(200));
    print_inputs();
    printf("RIG,PINTEST,%s,%d/%d steps - all outputs back to safe, dashboard control restored\n",
           s_abort ? "ABORTED" : "DONE", i, N_STEPS);
    s_running = false;
    vTaskDelete(NULL);
}

void pintest_command(const char *a1)
{
    if (a1 && !strcmp(a1, "STOP")) {
        if (!s_running) {
            all_safe();
            printf("OK,PINTEST,STOP,not running - outputs set safe anyway\n");
            return;
        }
        s_abort = true;
        printf("OK,PINTEST,STOP\n");
        return;
    }
    if (a1 && strcmp(a1, "RUN")) {
        printf("ERR,usage: PINTEST [RUN|STOP]\n");
        return;
    }
    if (s_running) {
        printf("ERR,PINTEST,already running - PINTEST STOP to abort\n");
        return;
    }
    s_running = true;
    s_abort = false;
    if (xTaskCreate(pintest_task, "pintest", 4096, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        s_running = false;
        printf("ERR,PINTEST,could not start the task\n");
        return;
    }
    printf("OK,PINTEST\n");
}
