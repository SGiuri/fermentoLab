#pragma once
// Mock di esp_rom_sys.h: codice hardware dell'ultimo reset (quello che la ROM stampa come "rst:0x..").
#include <cstdint>
typedef int soc_reset_reason_t;
extern int g_mock_hw_reset;
inline soc_reset_reason_t esp_rom_get_reset_reason(int) { return g_mock_hw_reset; }
