#pragma once
// Mock di soc/rtc.h: contatore RTC a 150 kHz, agganciato al tempo simulato. g_mock_rtc_zero_ms simula un reset
// del dominio RTC (il contatore riparte da zero).
#include <cstdint>
extern unsigned long g_mock_rtc_zero_ms;
uint64_t rtc_time_get();
inline uint32_t rtc_clk_slow_freq_get_hz() { return 150000; }
