#ifndef __NVM_H__
#define __NVM_H__

#include <stdint.h>

/**
 * @brief Inisialisasi non-volatile memory.
 *
 * @details Non-volatile memory merupakan memory yang tidak berubah walaupun tidak 
 * dialiri listrik sekalipun. Fungsi ini melakukan inisialisasi pada memory tersebut.
 *
 * @return tidak ada.
 */
void NVM_Init();

/**
 * @brief Menulis data pada non-volatile memory.
 *
 * @details Fungsi ini menulis data ke non-volatile memory
 * @param[in] address alamat yang akan ditulis
 * @param[in] data data yang akan ditulis
 * @param[in] length jumlah data yang akan ditulis
 *
 * @return status
 * @retval -1 jika gagal.
 * @retval 0 jika sukses.
 */
uint8_t NVM_Write(uint32_t address, const uint8_t* data, uint16_t length);

/**
 * @brief Membaca data pada non-volatile memory.
 *
 * @details Fungsi ini membaca data dari non-volatile memory
 * @param[in] address alamat yang akan dibaca
 * @param[out] data data yang akan dibaca
 * @param[in] length jumlah data yang akan dibaca
 *
 * @return status
 * @retval -1 jika gagal.
 * @retval 0 jika sukses.
 */
uint8_t NVM_Read(uint32_t address, uint8_t* data, uint16_t length);

#endif