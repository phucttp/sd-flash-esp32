/**
 * @file prog_esp32_crypto.h
 * @brief AES-128-CBC decryption for ESP32 encrypted firmware flashing.
 * @details Provides streaming decryption for reading .enc files from SD card
 *          and flashing to target ESP32 via UART.
 */

#ifndef PROG_ESP32_CRYPTO_H
#define PROG_ESP32_CRYPTO_H

#include "esp_err.h"
#include "mbedtls/aes.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief AES decryption context for streaming operations.
 */
typedef struct {
    mbedtls_aes_context aes_ctx;  // mbedtls AES context
    unsigned char iv[16];          // Current IV (updated after each block)
    bool initialized;              // Whether context is ready
} flasher_crypto_ctx_t;

/**
 * @brief Initialize AES decryption context with key/IV from SD config.
 * @param ctx Pointer to context structure
 * @return ESP_OK on success, ESP_FAIL on error
 *
 * @note Reads key from /config/aes_key.txt and IV from /config/aes_iv.txt
 */
esp_err_t flasher_crypto_init(flasher_crypto_ctx_t* ctx);

/**
 * @brief Decrypt a chunk of data using AES-128-CBC.
 * @param ctx Initialized context
 * @param in Input buffer (encrypted data, must be 16-byte aligned)
 * @param in_len Input length in bytes
 * @param out Output buffer (decrypted data, must be at least in_len bytes)
 * @param out_len Pointer to store actual output length
 * @return ESP_OK on success, ESP_FAIL on error
 *
 * @note Only processes complete 16-byte blocks. Remaining bytes are discarded.
 * @note IV is automatically updated for CBC chaining.
 */
esp_err_t flasher_crypto_decrypt(flasher_crypto_ctx_t* ctx,
                                  const uint8_t* in, size_t in_len,
                                  uint8_t* out, size_t* out_len);

/**
 * @brief Cleanup and free AES context resources.
 * @param ctx Context to cleanup
 */
void flasher_crypto_cleanup(flasher_crypto_ctx_t* ctx);

#endif // PROG_ESP32_CRYPTO_H
