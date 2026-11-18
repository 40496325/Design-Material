// pintest.h - PINTEST console command (v5.0): steps every crane output, prints every input.
// Run with the HV, motor and magnet supplies OFF (ICD Table 43 stage 4 handshake).
#pragma once

/** Console: PINTEST [RUN|STOP] (argument already upper-case, may be NULL). */
void pintest_command(const char *a1);
