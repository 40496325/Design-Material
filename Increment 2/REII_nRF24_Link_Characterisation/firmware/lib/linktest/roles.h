#pragma once

#if (defined(ROLE_RX) + defined(ROLE_TX) + defined(ROLE_SCANNER)) != 1
#error "Define exactly one of ROLE_RX / ROLE_TX / ROLE_SCANNER in platformio.ini"
#endif

namespace lt {
void roleSetup();
void roleLoop();
}  // namespace lt
