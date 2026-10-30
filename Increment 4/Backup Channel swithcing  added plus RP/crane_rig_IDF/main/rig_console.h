#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Installs the console driver (USB Serial/JTAG on COM12) and starts the line reader task. */
void rig_console_start(void);

#ifdef __cplusplus
}
#endif
