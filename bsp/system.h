#ifndef __SYSTEM_H__
#define __SYSTEM_H__
#include <stdint.h>

/**
 * @brief Inisialisasi sistem.
 *
 * @details Inisialisasi sistem saat awal.
 *
 * @return tidak ada.
 */
void System_Init();

/**
 * @brief fungsi delay.
 *
 * @details melakukan delay.
 * 
 * @param[in] ms jumlah delay dalam milliseconds.
 * 
 * @return tidak ada.
 */
void System_DelayMs(uint32_t ms);

/**
 * @brief Jumlah tick sejak inisialisasi sistem berjalan.
 *
 * @details Mengembalikan nilai tick sejak inisialisasi sistem berjalan.
 *
 * @return Nilai tick dalam milliseconds.
 */
uint32_t System_GetTickMs();

/**
 * @brief Frekuensi clock yang diinisialisasi sistem
 *
 * @details mengembalikan nilai clock frequency sistem
 * 
 * @return Nilai clock frequency dalam Hz
 */
uint32_t System_GetClockFreq();

#endif