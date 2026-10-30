// =============================================================================
// rf_link.h - crane side of the REII wireless link (ICD v1 IF10.0)
// =============================================================================
// Owns the nRF24L01+ on J1 and runs it as the primary receiver (PRX) on pipe 1
// 'REII2'. One FreeRTOS task does everything radio-related: polls STATUS/FIFO
// (IRQ is not wired), decodes 0xA5 command frames, loads the 0x81 telemetry
// ACK payload (ICD profile), prints machine-readable "RIG,..." lines, and runs
// the console commands / self-tests queued by rig_console.c.
// =============================================================================
#pragma once

#include "esp_err.h"
#include "mcp23017.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configures CE, attaches the radio to the (already initialised) SPI2 bus,
 * probes + configures it and starts the radio task.
 *
 * @param mcp     MCP23017 handle for CE on J3.3 (GPA0), or NULL if the
 *                expander did not probe (CE then only goes out on J7.10).
 */
esp_err_t rf_link_start(mcp23017_handle_t *mcp);

/** Queue one console line (e.g. "PA LOW") for the radio task. Non-blocking. */
void rf_link_submit_line(const char *line);

#ifdef __cplusplus
}
#endif
