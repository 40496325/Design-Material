// =============================================================================
// mcp_shared.h - one owner for the MCP23017 (U4, I2C 0x20) output latches
// =============================================================================
// Added with the drive electronics firmware (Increment 4, 17 Nov 2026).
//
// Why this exists: before this, rf_link.c kept its own copy of OLATA and wrote
// the WHOLE port A every time it moved the nRF CE line (GPA0), and it set
// IODIRA to "GPA0 output, everything else input". That was fine while CE was
// the only MCP pin in use, but DRV_EN (J3.4 = GPA1) now lives on the same port
// and the hoist brake (J4.8 = GPB5) on port B. Every CE write would have
// cleared DRV_EN, and every heartbeat CE re-check would have turned GPA1 back
// into an input.
//
// So every MCP pin now goes through this module: it keeps one shadow of
// IODIR / GPPU / OLAT per port behind a mutex, and every change is a
// read-modify-write of the shadow followed by a write of that register. The
// radio's CE line and the drive's DRV_EN / brake can never clobber each other.
//
// Register map (IOCON.BANK = 0, the reset default), datasheet Table 3-1:
//   IODIRA 0x00  IODIRB 0x01   1 = input, 0 = output
//   GPPUA  0x0C  GPPUB  0x0D   1 = 100 k pull-up on that input
//   GPIOA  0x12  GPIOB  0x13   pin levels (read)
//   OLATA  0x14  OLATB  0x15   output latch (write)
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mcp23017.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Take ownership of the expander. Call once, after mcp23017_probe() passed and
 *  before rf_link_start() / drive_start(). NULL = expander missing: every other
 *  call then returns ESP_ERR_INVALID_STATE. Writes the reset defaults (all
 *  inputs, no pull-ups, latches 0) into the shadows. */
esp_err_t mcp_shared_init(mcp23017_handle_t *mcp);

bool mcp_shared_ok(void);

/** Make one pin an output. The latch is written BEFORE the direction, so the
 *  pin comes up at initial_level without a glitch. */
esp_err_t mcp_shared_config_output(mcp23017_port_t port, int bit, int initial_level);

/** Make one pin an input, with or without the internal 100 k pull-up. */
esp_err_t mcp_shared_config_input(mcp23017_port_t port, int bit, bool pullup);

/** Drive one output pin; the other 7 bits of the port keep their shadow value. */
esp_err_t mcp_shared_write(mcp23017_port_t port, int bit, int level);

/** Read the actual pin levels of a port (GPIOx). */
esp_err_t mcp_shared_read_port(mcp23017_port_t port, uint8_t *out_levels);

/** Re-write IODIR, GPPU and OLAT of both ports from the shadows (recovery after
 *  an expander glitch / brown-out; used by the radio heartbeat CE check). */
esp_err_t mcp_shared_reapply(void);

/** Shadow of a port's output latch / direction (for status printouts). */
uint8_t mcp_shared_olat(mcp23017_port_t port);
uint8_t mcp_shared_iodir(mcp23017_port_t port);

#ifdef __cplusplus
}
#endif
