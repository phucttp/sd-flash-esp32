/**
 * @file flasher_esp_crypto.cpp
 * @brief AES-128-CBC decryption for ESP32 encrypted firmware flashing.
 */

#include "flasher_esp_crypto.h"
#include "../wifi_config/wifi_config.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "FLASH_CRYPTO";

esp_err_t flasher_crypto_init(flasher_crypto_ctx_t* ctx) {
    if (!ctx) {
        ESP_LOGE(TAG, "Null context pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Load key/IV from SD config files
    static char url[256], key[33], iv[33];
    wifi_config_get_params(url, key, iv);

    // Validate key/IV length (must be exactly 16 bytes for AES-128)
    if (strlen(key) != 16) {
        ESP_LOGE(TAG, "Invalid key length: %d (expected 16)", strlen(key));
        return ESP_FAIL;
    }
    if (strlen(iv) != 16) {
        ESP_LOGE(TAG, "Invalid IV length: %d (expected 16)", strlen(iv));
        return ESP_FAIL;
    }

    // Initialize mbedtls AES context
    mbedtls_aes_init(&ctx->aes_ctx);

    // Set decryption key (128-bit = 16 bytes)
    int ret = mbedtls_aes_setkey_dec(&ctx->aes_ctx, (const unsigned char*)key, 128);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to set AES key: %d", ret);
        mbedtls_aes_free(&ctx->aes_ctx);
        return ESP_FAIL;
    }

    // Copy IV (will be updated during CBC decryption)
    memcpy(ctx->iv, iv, 16);
    ctx->initialized = true;

    ESP_LOGI(TAG, "Crypto context initialized successfully");
    return ESP_OK;
}

esp_err_t flasher_crypto_decrypt(flasher_crypto_ctx_t* ctx,
                                  const uint8_t* in, size_t in_len,
                                  uint8_t* out, size_t* out_len) {
    if (!ctx || !ctx->initialized) {
        ESP_LOGE(TAG, "Context not initialized");
        return ESP_FAIL;
    }

    if (!in || !out || !out_len) {
        ESP_LOGE(TAG, "Null pointer argument");
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate number of complete 16-byte blocks
    size_t blocks = in_len / 16;

    if (blocks == 0) {
        // No complete blocks to decrypt
        *out_len = 0;
        if (in_len > 0) {
            ESP_LOGW(TAG, "Input too small for AES block: %d bytes", in_len);
        }
        return ESP_OK;
    }

    // Warn if data is not aligned (partial blocks will be ignored)
    if (in_len % 16 != 0) {
        ESP_LOGW(TAG, "Data not 16-byte aligned: %d bytes, processing %d blocks",
                 in_len, blocks);
    }

    // Perform AES-128-CBC decryption
    // Note: IV is automatically updated by mbedtls for CBC chaining
    int ret = mbedtls_aes_crypt_cbc(&ctx->aes_ctx,
                                     MBEDTLS_AES_DECRYPT,
                                     blocks * 16,
                                     ctx->iv,
                                     in,
                                     out);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES decrypt failed: %d", ret);
        *out_len = 0;
        return ESP_FAIL;
    }

    *out_len = blocks * 16;
    return ESP_OK;
}

void flasher_crypto_cleanup(flasher_crypto_ctx_t* ctx) {
    if (ctx && ctx->initialized) {
        mbedtls_aes_free(&ctx->aes_ctx);
        memset(ctx->iv, 0, 16);  // Clear IV for security
        ctx->initialized = false;
        ESP_LOGI(TAG, "Crypto context cleaned up");
    }
}
