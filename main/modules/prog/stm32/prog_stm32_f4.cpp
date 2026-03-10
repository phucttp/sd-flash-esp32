/**
 * @file prog_stm32_f4.cpp
 * @brief Engine nạp firmware cho STM32F4 qua giao thức SWD (Serial Wire Debug).
 *
 * Chức năng chính:
 *   - Phát hiện mức RDP (Read Data Protection) hiện tại của target STM32
 *   - Vô hiệu hóa RDP Level 1 bằng cách ghi mù (blind write) vào OPTCR — không đọc lại,
 *     vì toàn bộ AHB-AP read trả về 0x00 dưới RDP1; chỉ PPB/CoreSight còn khả dụng
 *   - Nạp firmware theo từng chunk 256 byte, xác thực ngay sau mỗi chunk, thử lại tối đa 3 lần
 *   - Hỗ trợ hai chiến lược RAM: full-malloc (toàn bộ file) và streaming 32KB (file lớn hơn RAM)
 *   - Dùng SYSRESETREQ giữa các lần retry để reset trạng thái flash controller về sạch
 */

#include "prog_stm32_f4.h"

#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Adafruit_DAP.h"
#include "Arduino.h"
#include <stdio.h>
#include <sys/stat.h>
#include "../../storage/usb_drive/usb_drive.h"

static const char *TAG = "SWD";

// ============================================================
// STM32 FLASH REGISTERS
// ============================================================
#define STM32_FLASH_KEYR    0x40023C04
#define STM32_FLASH_OPTKEYR 0x40023C08
#define STM32_FLASH_SR      0x40023C0C
#define STM32_FLASH_CR      0x40023C10
#define STM32_FLASH_OPTCR   0x40023C14

// Cortex-M debug registers (PPB — always accessible, even under RDP1)
#define DHCSR               0xE000EDF0      // Pause/Play" của CPU.
#define DCRSR               0xE000EDF4      // Chọn register để truy cập qua DCRDR (R0-R15, xPSR, MSP, PSP, etc).
#define DCRDR               0xE000EDF8      // Đọc/ghi dữ liệu vào register đã chọn bởi DCRSR.
#define DEMCR               0xE000EDFC      // Debug Exception and Monitor Control Register (VC_CORERESET bit để tự động halt sau reset)
#define AIRCR               0xE000ED0C      // Nút "Reset mềm".

// ============================================================
// DAP INSTANCE & STATE
// ============================================================
static Adafruit_DAP_STM32 dap;
static bool g_initialized = false;

#define SWD_CONNECT_MAX_RETRIES  10
#define SWD_CONNECT_RETRY_MS     500

static void dap_error_cb(const char *msg) {
    ESP_LOGE(TAG, "DAP: %s", msg);
}

// ============================================================
// INTERNAL HELPERS
// ============================================================
static void halt_core(void) {
    // Ghi vào DHCSR (Debug Halting Control & Status Register)
    // 0xA05F: Key bắt buộc để ghi
    // Bit 0 (C_DEBUGEN): Bật chế độ debug
    // Bit 1 (C_HALT): Ra lệnh dừng Core
    dap.dap_write_word(DHCSR, 0xA05F0003);  // C_HALT + C_DEBUGEN

    // Ghi vào DEMCR (Debug Exception and Monitor Control Register)
    // Bit 0 (VC_CORERESET): Bắt (Catch) vector reset 
    // -> Giúp kết nối được với chip ngay cả khi code cũ bị lỗi hoặc khóa chip
    dap.dap_write_word(DEMCR, 0x00000001);   // VC_CORERESET
    delay(15);
}

/**
 * @brief SWD Handshake: Gửi chuỗi JTAG-to-SWD -> Đọc IDCODE -> Validate tín hiệu.
 * Có cơ chế Retry để chống nhiễu vật lý.
 */
static esp_err_t connect_target(uint32_t *idcode) {
    for (int i = 1; i <= SWD_CONNECT_MAX_RETRIES; i++) {
        if (i > 1) {
            ESP_LOGW(TAG, "Retry %d/%d...", i, SWD_CONNECT_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(SWD_CONNECT_RETRY_MS));
        }
        
        // 1. Gửi chuỗi chuyển đổi JTAG -> SWD sequence
        // Fail ở đây -> Thường do đứt dây SWCLK hoặc Target mất nguồn.
        if (!dap.targetConnect()) {
            ESP_LOGW(TAG, "targetConnect() failed (attempt %d)", i);
            continue;
        }

        // 2. Đọc IDCODE từ Debug Port để xác nhận kết nối
        *idcode = 0;
        dap.dap_read_idcode(idcode);

        // 3. Validate IDCODE (Lọc nhiễu vật lý):
        // - 0x00000000: Lỗi Short GND hoặc hở mạch.
        // - 0xFFFFFFFF: Lỗi Floating hoặc Short VCC.
        if (*idcode != 0 && *idcode != 0xFFFFFFFF) {
            ESP_LOGI(TAG, "Connected! IDCODE=0x%08" PRIx32, *idcode);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Bad IDCODE: 0x%08" PRIx32, *idcode);
        dap.dap_disconnect();
    }

    ESP_LOGE(TAG, "Connect failed after %d attempts", SWD_CONNECT_MAX_RETRIES);
    return ESP_FAIL;
}

/**
 * @brief Thiết lập phiên làm việc đầy đủ: Physical Connect -> Halt Core -> Identify Chip.
 * Hàm này quan trọng để xác định xem chip là loại nào (F1/F4/G0) và có bị khóa RDP không.
 */
static esp_err_t full_connect(uint32_t *idcode, uint32_t *chip_id) {
    #define SELECT_MAX_RETRIES 5

    for (int attempt = 1; attempt <= SELECT_MAX_RETRIES; attempt++) {
        // 1. Thiết lập kết nối vật lý SWD (Physical Layer)
        esp_err_t ret = connect_target(idcode);
        if (ret != ESP_OK) return ret;

        // 2. Clear DP sticky errors (STKERR, WDERR, ORUNERR)
        // Nguồn nhiễu có thể gây AHB-AP error → sticky bit chặn mọi transaction sau
        dap.dap_write_reg(0x00, 0x0000001E);

        // 3. Halt Core → chờ AHB-AP ổn định
        halt_core();
        vTaskDelay(pdMS_TO_TICKS(50));

        // 4. Nhận diện dòng chip (Select Target)
        *chip_id = 0;
        if (dap.select(chip_id)) {
            halt_core();
            uint32_t flash_kb = dap.target_device.flash_size / 1024;
            if (flash_kb == 0) {
                ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: unknown (RDP blocking reads?)",
                         dap.target_device.name, *chip_id);
            } else {
                ESP_LOGI(TAG, "Chip: %s | ID: 0x%03" PRIx32 " | Flash: %" PRIu32 " KB",
                         dap.target_device.name, *chip_id, flash_kb);
            }
            return ESP_OK;
        }

        ESP_LOGW(TAG, "select() failed (ID: 0x%03" PRIx32 "), attempt %d/%d",
                 *chip_id, attempt, SELECT_MAX_RETRIES);

        // Full disconnect + delay → reconnect từ đầu (reset AHB-AP state)
        dap.dap_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGE(TAG, "Chip not recognized after %d attempts", SELECT_MAX_RETRIES);
    return ESP_FAIL;
}

/**
 * @brief Ngắt kết nối toàn diện: Logic Layer -> Physical Layer.
 * Đảm bảo an toàn điện và trả lại quyền điều khiển cho Target.
 */
static void full_disconnect(void) {
    // 1. Logic Disconnect: Hủy chọn Target (Deselect)
    // -> Báo cho DAP controller biết phiên làm việc đã kết thúc, reset trạng thái AP.
    dap.deselect();

    // 2. Physical Disconnect: Thả nổi chân GPIO (High-Z)
    // -> Đưa SWCLK/SWDIO về trạng thái Input/Float.
    // -> Cực kỳ quan trọng để không can thiệp điện áp (Short/Leakage) 
    //    khi Target chạy ứng dụng hoặc khi người dùng rút dây.
    dap.dap_disconnect();
}

// ============================================================
// INIT / DEINIT
// ============================================================

/**
 * @brief Khởi tạo lớp vật lý SWD (Physical Layer): GPIO, Clock, State.
 * Thiết lập chân tín hiệu về trạng thái Idle an toàn.
 */
esp_err_t flasher_swd_stm32f4_init(void) {
    // 1. Singleton Guard: Ngăn chặn init lại nhiều lần
    // -> Tránh việc cấu hình lại GPIO (reset state) làm xung nhiễu (Glitch) 
    //    đường bus khi đang có kết nối ngầm.
    if (g_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Init: SWCLK=GPIO%d SWDIO=GPIO%d nRESET=%d",
             SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN);
    
    // 2. Cấu hình GPIO & Buffer nội bộ:
    // - SWCLK: Output (Push-Pull).
    // - SWDIO: Input/Output (Open-Drain/Bidirectional) để tránh xung đột dòng điện.
    // - Đăng ký Error Callback để hứng log lỗi tầng thấp (Bit-bang timing).
    if (!dap.begin(SWD_SWCLK_PIN, SWD_SWDIO_PIN, SWD_NRESET_PIN, dap_error_cb)) {
        ESP_LOGE(TAG, "DAP begin() FAILED");
        return ESP_FAIL;
    }

    g_initialized = true;
    return ESP_OK;
}

/**
 * @brief Giải phóng tài nguyên SWD: GPIO -> High-Z (An toàn điện).
 * Đưa các chân tín hiệu về trạng thái thả nổi để tránh xung đột với Target.
 */
esp_err_t flasher_swd_stm32f4_deinit(void) {
    // 1. Singleton Guard: Tránh de-init nhiều lần (Idempotency).
    if (!g_initialized) return ESP_OK;

    // 2. Physical Release (High-Impedance):
    // -> Cấu hình lại chân SWCLK/SWDIO thành Input (Floating).
    // -> QUAN TRỌNG: Ngăn dòng điện rò (Leakage) hoặc xung đột logic (Bus Contention)
    //    khi Target đang chạy ứng dụng riêng hoặc khi người dùng rút cáp nóng.
    dap.dap_disconnect();
    g_initialized = false;
    ESP_LOGI(TAG, "SWD released");
    return ESP_OK;
}

// ============================================================
// DETECT RDP
// ============================================================
/**
 * @brief Giám định pháp y RDP: Đọc trạng thái khóa Flash (Level 0/1/2).
 * Phân tích thanh ghi OPTCR và kiểm tra thực tế khả năng đọc Flash.
 */
esp_err_t flasher_swd_stm32f4_detect_rdp(int *rdp_level) {
    ESP_LOGI(TAG, "======= DETECT RDP =======");

    // 1. Init & Connect: Cần kết nối ổn định để đọc thanh ghi ngoại vi
    esp_err_t ret = flasher_swd_stm32f4_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        flasher_swd_stm32f4_deinit();
        return ret;
    }

    // 2. Đọc thanh ghi cấu hình Option Bytes (OPTCR)
    // Lấy byte RDP tại bit [15:8].
    uint32_t optcr = dap.dap_read_word(STM32_FLASH_OPTCR);
    uint8_t rdp_byte = (optcr >> 8) & 0xFF;

    ESP_LOGI(TAG, "OPTCR=0x%08" PRIx32 " RDP=0x%02X", optcr, rdp_byte);

    // 3. Phân loại mức độ bảo vệ:
    // - 0xAA: Level 0 (Sạch - Mở hoàn toàn).
    // - 0xCC: Level 2 (Tử vong - Khóa vĩnh viễn, JTAG fuse bị đốt).
    // - Khác : Level 1 (Khóa thường - Có thể cứu bằng Mass Erase).
    // * LƯU Ý: Ở Level 1, một số chip chặn luôn việc đọc OPTCR (trả về 0).
    int level;
    if (rdp_byte == 0xAA) {
        level = 0;
        ESP_LOGI(TAG, ">>> RDP Level 0 — NO protection");
    } else if (rdp_byte == 0xCC) {
        level = 2;
        ESP_LOGE(TAG, ">>> RDP Level 2 — PERMANENT!");
    } else {
        level = 1;
        ESP_LOGW(TAG, ">>> RDP Level 1 — flash blocked via debug");
        ESP_LOGW(TAG, "    (OPTCR=0x00000000 under RDP1 is normal)");
    }

    // 4. Kiểm chứng thực tế (Confirmation Test):
    // Thử đọc địa chỉ đầu Flash (0x08000000).
    // - Nếu đọc ra 0x00000000: AHB-AP bị chặn -> Chắc chắn là RDP Level 1.
    // - Nếu đọc ra dữ liệu thật: Chip đang mở.
    uint32_t flash_word = dap.dap_read_word(0x08000000);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32 " %s",
             flash_word, (flash_word == 0) ? "(blocked?)" : "(readable)");

    if (rdp_level) *rdp_level = level;
    
    // 5. Cleanup: Ngắt kết nối an toàn
    full_disconnect();
    flasher_swd_stm32f4_deinit();

    ESP_LOGI(TAG, "======= DETECT RDP DONE (Level %d) =======", level);
    return ESP_OK;
}

// ============================================================
// GIAI ĐOẠN 1: KÍCH HOẠT GỠ BỎ RDP (KỸ THUẬT GHI MÙ - BLIND WRITES)
//
// Đặc điểm RDP Level 1: Mọi lệnh ĐỌC qua AHB-AP đều trả về 0 (bị chặn),
// NHƯNG lệnh GHI vẫn đi qua được -> Đây là lỗ hổng để cứu chip.
//
// Chiến thuật tác chiến:
//   1. Kết nối SWD, khởi tạo AP, cưỡng chế dừng Core (Halt).
//   2. SYSRESETREQ -> Reset mềm để Flash Controller về trạng thái sạch (Locked).
//      (Tránh trường hợp nhập Key bị sai do trạng thái cũ còn lưu).
//   3. Dừng Core lại ngay lập tức bằng VC_CORERESET (trước khi Firmware kịp chạy).
//   4. Ghi mù: Nạp Key mở khóa Flash (2 words: KEY1, KEY2).
//   5. Ghi mù: Nạp Key mở khóa Option Bytes (2 words).
//   6. Ghi mù: OPTCR = 0x0FFFAAEE (Set RDP=0xAA + Kích hoạt lệnh Start).
//   7. TUYỆT ĐỐI KHÔNG ĐỌC AHB-AP (Vì sẽ trả về 0 và làm hỏng con trỏ địa chỉ TAR).
//   8. Chờ 15s cho chip tự Mass Erase (Dùng lệnh đọc IDCODE ở tầng DP để giữ kết nối).
//   9. Ngắt kết nối -> Yêu cầu người dùng rút điện cắm lại (Power Cycle) -> Kiểm tra.
// ============================================================

/**
 * @brief Kích hoạt quy trình Unlock RDP (Blind Write Sequence).
 * @note  Dành cho chip đang bị khóa Level 1 (Read Protected).
 * Chiến thuật: Ghi đè liên tục các Key mở khóa mà KHÔNG cần đọc lại kiểm tra.
 */
esp_err_t flasher_swd_stm32f4_rdp_disable_trigger(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: TRIGGER (BLIND WRITES) =======");

    // 1. Khởi tạo & Kết nối vật lý
    esp_err_t ret = flasher_swd_stm32f4_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0;
    ret = connect_target(&idcode);
    if (ret != ESP_OK) {
        flasher_swd_stm32f4_deinit();
        return ret;
    }

    // 2. Chuẩn bị tầng AP (Access Port)
    // Cấp xung, bật nguồn Debug Power để sẵn sàng ghi vào AHB Bus.
    dap.dap_target_prepare();

    // 3. Halt Core & Reset sạch sẽ
    // Mục tiêu: Đưa Flash Controller về trạng thái ổn định nhất.
    halt_core();

    // Gửi lệnh SYSRESETREQ: Reset mềm toàn hệ thống.
    // -> Xóa sạch các trạng thái treo (Pending) của Flash Controller.
    // -> Đảm bảo VC_CORERESET sẽ tóm CPU ngay tại vector 0x04 sau khi reset.
    ESP_LOGI(TAG, "Core halted, sending SYSRESETREQ...");
    dap.dap_write_word(AIRCR, 0x05FA0004);
    delay(50);  // Chờ Reset hoàn tất

    // Halt lại cho chắc (dù VC_CORERESET đã bắt rồi)
    halt_core();
    ESP_LOGI(TAG, "Core re-halted after reset");

    // ============================================================
    // CHIẾN THUẬT GHI MÙ (BLIND WRITE) - CƠ CHẾ 3 LỚP BẢO VỆ
    // 1. NO READ: Không đọc lại để tránh làm lệch con trỏ TAR (Auto-increment).
    // 2. RETRY: Thử 3 lần phòng trường hợp rớt gói tin (Bit-bang glitch).
    // 3. RESET STATE: Reset chip giữa các lần thử để xóa bộ nhớ đệm Key.
    // ============================================================
    #define RDP_BLIND_WRITE_ATTEMPTS  3

    for (int bw = 1; bw <= RDP_BLIND_WRITE_ATTEMPTS; bw++) {
        // Nếu không phải lần đầu, phải Reset để xóa trạng thái "Sai Key" của lần trước
        if (bw > 1) {
            // Reset flash controller state machine via SYSRESETREQ
            ESP_LOGW(TAG, "Retry %d/%d: resetting chip first...", bw, RDP_BLIND_WRITE_ATTEMPTS);
            dap.dap_write_word(AIRCR, 0x05FA0004);  // SYSRESETREQ
            delay(50);
            halt_core();
        }

        ESP_LOGI(TAG, "Blind write attempt %d/%d...", bw, RDP_BLIND_WRITE_ATTEMPTS);

        // A. Nạp Flash Unlock Keys (KEY1 -> KEY2)
        // Yêu cầu: Phải đến liên tiếp, không được rớt gói nào.
        dap.dap_write_word(STM32_FLASH_KEYR, 0x45670123);   // Key 1
        dap.dap_write_word(STM32_FLASH_KEYR, 0xCDEF89AB);   // Key 2

        // B. Nạp Option Byte Unlock Keys
        dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x08192A3B); // Opt key 1
        dap.dap_write_word(STM32_FLASH_OPTKEYR, 0x4C5D6E7F); // Opt key 2

        // C. Ghi đè OPTCR: Set RDP=Level 0 (0xAA) + Start Bit
        // Giá trị 0x0FFFAAEE:
        // - 0xAA (Bits 15:8): RDP Level 0
        // - Bit 1 (OPTSTRT): Kích hoạt lệnh ngay lập tức
        // -> Chip sẽ tự động kích hoạt Mass Erase.
        dap.dap_write_word(STM32_FLASH_OPTCR, 0x0FFFAAEE);
    }

    ESP_LOGI(TAG, "Blind writes sent (%dx with reset between)", RDP_BLIND_WRITE_ATTEMPTS);

    // ============================================================
    // GIAI ĐOẠN 2: CHỜ MASS ERASE (15s)
    // Chip đang bận xóa toàn bộ Flash -> Bus AHB bị khóa (BUSY).
    // Chiến thuật: Chỉ ping IDCODE (tầng DP) để giữ kết nối (Keep-alive).
    // TUYỆT ĐỐI KHÔNG: Đọc vào vùng nhớ Flash lúc này -> Treo Bus.
    // ============================================================

    ESP_LOGI(TAG, "Waiting 15s for mass erase... DO NOT touch STM32!");
    for (int i = 15; i > 0; i--) {
        uint32_t id = 0;
        dap.dap_read_idcode(&id);  // DP level — always works
        ESP_LOGI(TAG, "  %ds... (IDCODE=0x%08" PRIx32 ")", i, id);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Kết thúc: Ngắt kết nối để người dùng reset cứng
    dap.dap_disconnect();
    flasher_swd_stm32f4_deinit();

    ESP_LOGI(TAG, "======= TRIGGER DONE =======");
    ESP_LOGW(TAG, ">>> POWER CYCLE STM32 now! <<<");
    return ESP_OK;
}

/**
 * @brief RESCUE ERASE: Kích hoạt xóa thủ công khi chip đã mở (RDP L0)
 * nhưng dữ liệu rác vẫn còn (do quá trình Auto-Erase trước đó bị ngắt quãng).
 * Lưu ý: Chỉ chạy được khi RDP = Level 0 (đã mở khóa AHB).
 */

static esp_err_t perform_rescue_erase(void) {
    ESP_LOGW(TAG, ">>> Flash has data after RDP disable — rescue erase!");

    // 1. Mở khóa Flash Control Register (CR)
    // Ghi chuỗi Key magic vào KEYR để hạ cờ LOCK (Bit 31).
    dap.dap_write_word(STM32_FLASH_KEYR, 0x45670123);
    dap.dap_write_word(STM32_FLASH_KEYR, 0xCDEF89AB);

    // 2. Xác minh trạng thái Unlock
    // Đọc lại CR. Nếu bit LOCK (31) vẫn là 1 -> Sai Key hoặc lỗi đường truyền.
    uint32_t cr = dap.dap_read_word(STM32_FLASH_CR);
    if (cr & (1UL << 31)) {
        ESP_LOGE(TAG, "Flash unlock FAILED! CR=0x%08" PRIx32, cr);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Flash unlocked (CR=0x%08" PRIx32 ")", cr);

    // 3. Vệ sinh thanh ghi trạng thái (Status Register - SR)
    // Ghi 1 vào các cờ lỗi (PGSERR, PGPERR...) để xóa chúng (Write-1-to-Clear).
    // -> Đảm bảo không dính lỗi cũ từ phiên làm việc trước làm treo lệnh Erase mới.
    dap.dap_write_word(STM32_FLASH_SR, 0xF3);

    // 4. Kích hoạt Mass Erase (Xóa toàn bộ Bank)
    // Cấu hình: MER (Mass Erase) + PSIZE x32 (Program Size Word).
    // * QUAN TRỌNG: PSIZE x32 yêu cầu điện áp VCC > 2.7V. Nếu áp thấp phải đổi về x8.
    
    // Bước 4a: Set MER + PSIZE (Chuẩn bị nòng súng)
    dap.dap_write_word(STM32_FLASH_CR, 0x00000204);  // MER + PSIZE=x32

    // Bước 4b: Set STRT (Kích hoạt - Bóp cò)
    dap.dap_write_word(STM32_FLASH_CR, 0x00010204);  // MER + PSIZE=x32 + STRT

    ESP_LOGI(TAG, "Erase started, waiting for BSY...");

    // 5. Vòng lặp chờ BSY (Busy Flag) hạ xuống 0
    // Thời gian xóa full chip F4 (1MB) có thể mất tới 10-20s.
    for (int t = 0; t < 200; t++) {
        uint32_t sr = dap.dap_read_word(STM32_FLASH_SR);

        // Kiểm tra bit 16 (BSY): 1 = Đang xóa, 0 = Xong/Nhàn rỗi
        if (sr & (1UL << 16)) {
            // Delay nhỏ để tránh spam bus quá mức
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Flash đã dừng, kiểm tra xem có lỗi không?
        // Check bit 0,1,4,5,6,7 (PGSERR, PGPERR, WRPERR...)
        if (sr & 0xF0) {  
            ESP_LOGE(TAG, "Erase error! SR=0x%08" PRIx32, sr);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, ">>> RESCUE ERASE SUCCESS! (SR=0x%08" PRIx32 ")", sr);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Erase timeout (20s)!");
    return ESP_FAIL;
}

/**
 * @brief GIAI ĐOẠN 2: THẨM ĐỊNH KẾT QUẢ UNLOCK.
 * Kiểm tra sau khi người dùng đã rút điện cắm lại (Power Cycle).
 * Đảm bảo RDP thực sự về 0xAA và Flash đã được xóa sạch.
 */

esp_err_t flasher_swd_stm32f4_rdp_disable_verify(void) {
    ESP_LOGI(TAG, "======= RDP DISABLE: VERIFY =======");

    // 1. Kết nối lại hệ thống
    esp_err_t ret = flasher_swd_stm32f4_init();
    if (ret != ESP_OK) return ret;

    uint32_t idcode = 0;
    ret = connect_target(&idcode);
    if (ret != ESP_OK) {
        // Nếu không kết nối được: Thường do người dùng chưa rút điện cắm lại
        // hoặc dây nối bị lỏng trong quá trình thao tác.
        ESP_LOGE(TAG, "Reconnect FAILED — did you power cycle?");
        flasher_swd_stm32f4_deinit();
        return ESP_FAIL;
    }

    // Chuẩn bị AP và dừng Core để chiếm quyền truy cập Bus
    dap.dap_target_prepare();
    halt_core();

    // 2. Đọc giá trị thực tế của RDP từ NVM (Non-Volatile Memory)
    // Sau Power Cycle, giá trị mới từ Flash Option Bytes mới được nạp vào thanh ghi bóng (Shadow Register).
    uint32_t optcr = dap.dap_read_word(STM32_FLASH_OPTCR);
    uint8_t rdp_byte = (optcr >> 8) & 0xFF;
    ESP_LOGI(TAG, "OPTCR=0x%08" PRIx32 " RDP=0x%02X", optcr, rdp_byte);

    // Kiểm tra xem đã về Level 0 (0xAA) chưa
    if (rdp_byte != 0xAA) {
        ESP_LOGE(TAG, "RDP STILL ACTIVE (0x%02X)!", rdp_byte);
        if (optcr == 0x00000000) {
            ESP_LOGE(TAG, "OPTCR=0 → RDP1 still blocking reads.");
            ESP_LOGE(TAG, "Writes may not have reached flash controller.");
        } else {
            ESP_LOGE(TAG, "Try: power cycle STM32 completely, then verify again.");
        }
        dap.dap_disconnect();
        flasher_swd_stm32f4_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> RDP Level 0 confirmed!");

    // 3. Kiểm tra tính toàn vẹn của Flash (Auto-Erase Check)
    // Nguyên tắc: Mở khóa RDP1 -> RDP0 thì chip PHẢI tự xóa trắng Flash
    uint32_t flash_word = dap.dap_read_word(0x08000000);
    ESP_LOGI(TAG, "Flash[0x08000000]=0x%08" PRIx32, flash_word);

    if (flash_word != 0xFFFFFFFF) {
        // Tình huống "Zombie": RDP đã mở nhưng dữ liệu vẫn còn (do Auto-erase lỗi).
        // Cần đội cứu hộ (Rescue Erase) vào dọn dẹp thủ công.
        ret = perform_rescue_erase();
        if (ret == ESP_OK) {
            flash_word = dap.dap_read_word(0x08000000);
            ESP_LOGI(TAG, "After rescue: Flash[0x08000000]=0x%08" PRIx32, flash_word);
            if (flash_word == 0xFFFFFFFF) {
                ESP_LOGI(TAG, ">>> RECOVERY SUCCESSFUL! Chip is clean.");
            } else {
                ESP_LOGE(TAG, "Flash still has data after rescue erase.");
            }
        } else {
            ESP_LOGE(TAG, "Rescue erase failed. Try power cycling again.");
        }
    } else {
        // Flash đã trắng tinh (0xFFFFFFFF) -> Quá trình tự động của chip đã thành công.
        ESP_LOGI(TAG, "Flash is clean (0xFFFFFFFF). All good!");
    }

    dap.dap_disconnect();
    flasher_swd_stm32f4_deinit();

    ESP_LOGI(TAG, "======= VERIFY DONE =======");
    return ESP_OK;
}

// ============================================================
// HÀM NẠP FIRMWARE STM32 QUA GIAO THỨC SWD (Serial Wire Debug)
// ============================================================
/**
 * @brief Nạp firmware vào chip STM32 qua giao thức SWD với cơ chế tự động sửa lỗi
 *
 * @details
 * Hàm này thực hiện quy trình hoàn chỉnh để nạp firmware từ SD card vào flash của STM32:
 *
 * **LUỒNG XỬ LÝ (5 BƯỚC CHÍNH):**
 * 1. MỞ FILE & QUẢN LÝ RAM (Dual Strategy):
 *    - Thử cấp phát toàn bộ firmware vào RAM (mode nhanh)
 *    - Nếu RAM không đủ → chuyển sang Streaming mode (đọc từng đoạn 32KB)
 *
 * 2. WARM-UP & KIỂM TRA RDP:
 *    - Gọi detect_rdp() để "đánh thức" khối debug của chip
 *    - Kiểm tra Read Protection Level (PHẢI là Level 0 mới được nạp)
 *    - Nếu Level 1/2 → dừng ngay, yêu cầu unlock trước
 *
 * 3. KẾT NỐI SWD:
 *    - Init SWD interface (SWDIO=GPIO0, SWCLK=GPIO3)
 *    - Connect + identify chip (đọc IDCODE, chip_id, flash_size)
 *    - Validate: firmware size ≤ flash size
 *
 * 4. XÓA FLASH (SECTOR ERASE):
 *    - Gọi program_start() để xóa các sectors cần thiết
 *    - Lưu ý: STM32 xóa TOÀN BỘ SECTOR (16KB/64KB/128KB)
 *    - Không thể xóa từng byte → dev phải thiết kế memory layout cẩn thận!
 *
 * 5. NẠP + VERIFY ĐỒNG THỜI (On-the-fly):
 *    - Chia firmware thành chunks 256B (mỗi chunk = 1 lần flash_unlock/PG)
 *    - Mỗi chunk: programBlock() → delay(1ms) → dap_read_block() → memcmp()
 *    - Nếu sai lệch → retry tối đa 3 lần tại chỗ
 *    - Nếu vẫn fail → xóa lại toàn bộ + nạp lại (tối đa 3 attempts)
 *
 * **TẠI SAO CẦN VERIFY ON-THE-FLY?**
 * - ESP32-C3 bit-bang SWD có tỷ lệ lỗi ~1/1000 words (dap_write_word silent fail)
 * - Không thể tin tưởng 100% vào programBlock() → PHẢI verify ngay sau khi ghi
 * - Verify ngay giúp phát hiện lỗi sớm → retry kịp thời
 * - Verify sau cùng (cuối file) thì đã muộn → phải xóa + nạp lại toàn bộ
 *
 * **DUAL RAM STRATEGY (Quan Trọng):**
 * - Full-RAM mode: malloc(toàn bộ file) → đọc hết vào RAM → đóng SD → nạp
 *   → Ưu điểm: Nhanh (không cần đọc SD nhiều lần), tránh xung đột SPI
 *   → Nhược điểm: Firmware phải ≤ RAM còn trống (~200KB trên ESP32-C3)
 *
 * - Streaming mode: malloc(32KB) → đọc từng segment từ SD trong lúc nạp
 *   → Ưu điểm: Hỗ trợ firmware BẤT KỲ kích thước (chỉ cần ≤ flash target)
 *   → Nhược điểm: Chậm hơn (nhiều lần đọc SD), phải giữ file mở
 *
 * **AN TOÀN SWD + SD SPI:**
 * - SWDIO=GPIO0 (KHÔNG PHẢI GPIO2=FSPIQ) → không xung đột với SD SPI
 * - SD SPI pins: CS=GPIO7, CLK=GPIO6, MOSI=GPIO7, MISO=GPIO2
 * - Cả 2 giao thức hoạt động đồng thời an toàn trong Streaming mode
 *
 * **YÊU CẦU QUAN TRỌNG:**
 * - Chip STM32 phải ở RDP Level 0 (unlocked)
 * - Nếu Level 1/2 → chạy action_erase_chip_swd() trước (unlock + mass erase)
 * - Nguồn target PHẢI là 3.3V (KHÔNG dùng 5V → gây lỗi voltage mismatch!)
 * - Wiring đúng: SWDIO→PA13, SWCLK→PA14
 *
 * @param fw_path      Đường dẫn tuyệt đối file firmware trên SD card (VD: "/fw/app.bin")
 * @param on_progress  Callback hàm để cập nhật % tiến độ lên OLED (có thể NULL)
 *
 * @return
 * - ESP_OK: Nạp thành công, đã verify toàn bộ
 * - ESP_ERR_NOT_FOUND: File không tồn tại
 * - ESP_ERR_NO_MEM: Không đủ RAM (thậm chí cho 32KB buffer)
 * - ESP_ERR_INVALID_STATE: Chip bị khóa RDP Level 1/2
 * - ESP_FAIL: Nạp thất bại sau 3 attempts (lỗi SWD, target không phản hồi)
 *
 * @note
 * - Hàm này BLOCKING (chạy 2-5 phút tùy firmware size)
 * - Không được ngắt giữa chừng (có thể brick chip target!)
 * - Sau khi nạp xong, target sẽ reset tự động (nếu có nRESET pin)
 *
 * @warning
 * - program_start() xóa TOÀN BỘ SECTOR chứa firmware
 * - Nếu config data nằm cùng sector → sẽ bị xóa!
 * - Dev phải thiết kế linker script đúng: firmware ≤256KB, config từ sector 5+
 */
esp_err_t flasher_swd_stm32f4_flash_firmware(const std::string& fw_path,
                                      flasher_swd_stm32f4_progress_cb_t on_progress) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  STM32 SWD FLASH");
    ESP_LOGI(TAG, "  File: %s", fw_path.c_str());
    ESP_LOGI(TAG, "========================================");

    // ============================================================
    // CONSTANTS - Tham số điều chỉnh hành vi nạp firmware
    // ============================================================
    // Đoạn đệm 32KB: Kích thước mỗi segment đọc từ SD trong Streaming mode
    // Giá trị này cân bằng giữa: RAM usage vs số lần đọc SD
    // Lớn hơn = ít lần đọc SD nhưng tốn RAM. Nhỏ hơn = nhiều lần đọc SD.
    #define SWD_SEGMENT_SIZE   (32 * 1024)

    // Kích thước chunk nạp: 256B = 64 words = tối ưu cho flash controller STM32
    // Mỗi chunk = 1 lần flash_unlock + set PG bit + ghi 64 words + flash_lock
    // Nhỏ hơn (128B) = nhiều lần unlock/lock → chậm, lỗi cao hơn
    // Lớn hơn (512B) = cascade errors nếu 1 word fail → khó retry
    #define SWD_PROGRAM_CHUNK  256

    // Số lần thử nạp LẠI TOÀN BỘ FILE nếu attempt hiện tại fail
    // Mỗi retry = erase lại + nạp lại từ đầu + verify lại
    // 3 attempts = tổng tối đa 3 × (erase + program + verify)
    #define SWD_MAX_ATTEMPTS   3

    // Số lần thử RETRY TẠI CHỖ cho MỖI CHUNK 256B khi verify fail
    // Giúp sửa lỗi ngay khi phát hiện, tránh phải nạp lại toàn bộ file
    // Thực tế: ~3/72 chunks cần 1 retry, hiếm khi cần 2-3 retries
    #define SWD_CHUNK_RETRIES  3

    // Lambda function wrapper cho callback tiến độ
    // Kiểm tra NULL trước khi gọi → tránh crash nếu caller không truyền callback
    auto progress = [&](const char* text, int pct) {
        if (on_progress) on_progress(text, pct);
    };

    // ============================================================
    // BƯỚC 1/5: MỞ FILE & QUẢN LÝ BỘ NHỚ (DUAL RAM STRATEGY)
    // ============================================================
    // Chiến lược 2 tầng để xử lý firmware BẤT KỲ kích thước:
    //
    // TẦNG 1 (ƯU TIÊN): FULL-RAM MODE
    // - Thử malloc(toàn bộ file firmware)
    // - Nếu thành công:
    //   → Đọc TOÀN BỘ file vào RAM (1 lần đọc SD duy nhất)
    //   → Đóng file SD ngay → giải phóng SPI bus
    //   → Nạp từ RAM → NHANH NHẤT, không có xung đột SPI
    // - Phù hợp: Firmware ≤ 100KB trên ESP32-C3 (còn ~200KB RAM free)
    //
    // TẦNG 2 (FALLBACK): STREAMING MODE
    // - Nếu malloc(full file) FAIL → RAM không đủ
    // - Malloc(32KB segment) thay thế
    // - Giữ file SD MỞ trong suốt quá trình nạp
    // - Đọc từng segment 32KB → nạp → đọc segment tiếp theo → loop
    // - Phù hợp: Firmware BẤT KỲ (256KB, 512KB, 1MB...) chỉ cần ≤ flash target
    //
    // LƯU Ý AN TOÀN:
    // - SWDIO=GPIO0 (không phải GPIO2=FSPIQ mặc định của ESP32-C3)
    // - SD SPI dùng GPIO6(CLK), GPIO7(MOSI/CS), GPIO2(MISO=FSPIQ)
    // - Vì SWDIO không trùng với bất kỳ FSPI pin nào → 2 giao thức KHÔNG xung đột
    // - Đã test ổn định với Streaming mode: SWD + SD SPI chạy song song OK
    progress("Reading SD...", 1);
    ESP_LOGI(TAG, "[1/5] Opening firmware file...");

    // Mở file firmware từ VFS POSIX (/usb mount point)
    std::string fw_full_path = std::string(USB_DRIVE_MOUNT) + fw_path;
    FILE* fwFile = fopen(fw_full_path.c_str(), "rb");
    if (!fwFile) {
        ESP_LOGE(TAG, "FAIL: Cannot open %s", fw_full_path.c_str());
        progress("File not found!", 0);
        return ESP_ERR_NOT_FOUND;
    }

    // Đọc kích thước file qua stat
    struct stat fw_st;
    stat(fw_full_path.c_str(), &fw_st);
    size_t file_size = (size_t)fw_st.st_size;
    if (file_size == 0) {
        ESP_LOGE(TAG, "FAIL: File is empty!");
        fclose(fwFile);
        progress("File empty!", 0);
        return ESP_FAIL;
    }

    // --- PADDING TO 4-BYTE ALIGNMENT ---
    // STM32 flash programming hoạt động ở WORD level (32-bit = 4 bytes)
    // Nếu file không chia hết cho 4 → phải pad thêm 0xFF vào cuối
    // Ví dụ: file_size = 65537 bytes → padded_size = 65540 bytes (thêm 3 bytes 0xFF)
    // Công thức: (size + 3) & ~3 = làm tròn LÊN đến bội số 4 gần nhất
    size_t padded_size = (file_size + 3) & ~3;

    // --- DUAL RAM STRATEGY: Thử Full-RAM trước, fallback sang Streaming ---
    uint8_t* fw_buf = (uint8_t*)malloc(padded_size);
    bool streaming = false;  // Flag: true = Streaming mode, false = Full-RAM mode
    size_t buf_size;         // Kích thước buffer thực tế (full hoặc 32KB)

    if (fw_buf) {
        // ========================================================
        // FULL-RAM MODE (ƯU TIÊN) - Nhanh nhất, không xung đột SPI
        // ========================================================
        buf_size = padded_size;

        // Pad phần cuối file bằng 0xFF (erased flash value)
        // Đảm bảo alignment 4-byte và không có garbage data
        memset(fw_buf + file_size, 0xFF, padded_size - file_size); // Điền đầy những chỗ trống còn thừa ở cuối bộ đệm bằng giá trị 0xFF

        // Đọc TOÀN BỘ file vào RAM trong 1 lần (hoặc nhiều lần nếu file lớn)
        // SD.read() có thể trả về ít hơn số byte yêu cầu → cần loop đến hết
        size_t total_read = 0;
        while (total_read < file_size) {
            size_t n = fread(fw_buf + total_read, 1, file_size - total_read, fwFile);
            if (n == 0) break;  // EOF hoặc SD error
            total_read += n;
        }

        // Đóng file NGAY sau khi đọc xong → giải phóng SPI bus
        // Từ đây trở đi không cần SD card nữa → nạp từ RAM
        fclose(fwFile);

        // Validate: Đảm bảo đọc đủ số byte
        // Nếu thiếu → có thể SD card lỗi, file corrupt, hoặc unmount giữa chừng
        if (total_read != file_size) {
            ESP_LOGE(TAG, "FAIL: SD read incomplete: %zu / %zu bytes", total_read, file_size);
            free(fw_buf);
            progress("SD read error!", 0);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "      Full-RAM: %zu bytes (%" PRIu32 " KB)",
                 file_size, (uint32_t)(file_size / 1024));
    } else {
        // ========================================================
        // STREAMING MODE (FALLBACK) - Chậm hơn nhưng hỗ trợ FW lớn
        // ========================================================
        // malloc(full file) FAIL → RAM không đủ
        // Thử malloc buffer nhỏ hơn (32KB) để đọc từng segment

        fw_buf = (uint8_t*)malloc(SWD_SEGMENT_SIZE);
        if (!fw_buf) {
            // Nếu thậm chí 32KB cũng không cấp phát được
            // → Hệ thống đang quá ít RAM (có thể memory leak)
            ESP_LOGE(TAG, "FAIL: Cannot allocate even %d bytes", SWD_SEGMENT_SIZE);
            fclose(fwFile);
            progress("Out of memory!", 0);
            return ESP_ERR_NO_MEM;
        }

        buf_size = SWD_SEGMENT_SIZE;
        streaming = true;  // Flag cho các đoạn code sau biết phải đọc SD liên tục

        // QUAN TRỌNG: KHÔNG đóng file ở đây!
        // File sẽ được GIỮ MỞ trong suốt quá trình nạp
        // Mỗi lần cần data → đọc 32KB tiếp theo từ SD
        // seek() không cần thiết vì đọc tuần tự (sequential read)

        ESP_LOGI(TAG, "      Streaming: %zu bytes FW, %d byte segments",
                 file_size, SWD_SEGMENT_SIZE);
    }

    // ============================================================
    // BƯỚC 2/5: SWD WARM-UP & KIỂM TRA RDP (Read Protection)
    // ============================================================
    // Tại sao cần "warm-up"?
    // - Chip STM32 sau khi power-on hoặc reset: debug interface có thể ở trạng thái "ngủ"
    // - Nếu kết nối SWD ngay → tỷ lệ thất bại cao (~30-50%)
    // - detect_rdp() thực hiện 1 chu kỳ HOÀN CHỈNH:
    //   init → connect → select → đọc OPTCR → disconnect → deinit
    // - Chu kỳ này "đánh thức" debug interface, reset state machine
    // - Sau warm-up, kết nối tiếp theo (BƯỚC 3) thành công gần 100%
    //
    // Read Protection Level (RDP) là gì?
    // - RDP = cơ chế bảo vệ flash của STM32 khỏi bị đọc qua JTAG/SWD
    // - 3 levels:
    //   Level 0: Không bảo vệ, cho phép đọc/ghi flash qua SWD ✅
    //   Level 1: Chặn đọc flash, CHỈ CHO PHÉP GHI (blind writes) ⚠️
    //   Level 2: Khóa vĩnh viễn, không thể unlock (chỉ có full chip erase) ❌
    //
    // Tại sao PHẢI là Level 0?
    // - Level 1/2: Không thể VERIFY sau khi program → không biết ghi đúng hay sai
    // - Level 1: Có thể ghi nhưng không đọc được → on-the-fly verify FAIL
    // - Level 0: Đọc/ghi tự do → verify OK → đảm bảo firmware chính xác 100%
    //
    // Nếu chip bị khóa Level 1/2 phải làm gì?
    // → Chạy action_erase_chip_swd() trước:
    //    - Unlock qua blind writes (FLASH_KEYR + FLASH_OPTKEYR + OPTCR)
    //    - Mass erase toàn bộ flash (10-15s)
    //    - Power cycle (RÚT ĐIỆN, không phải reset button!)
    //    - Sau đó chip về Level 0 → có thể nạp firmware bình thường
    progress("Detecting...", 3);
    ESP_LOGI(TAG, "[2/5] Detecting chip + RDP (warm-up)...");

    int rdp_level = -1;
    esp_err_t ret = flasher_swd_stm32f4_detect_rdp(&rdp_level);
    if (ret != ESP_OK) {
        // Không kết nối được SWD → kiểm tra wiring, nguồn, target power
        ESP_LOGE(TAG, "FAIL: Cannot connect to STM32");
        progress("Connect fail!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    if (rdp_level != 0) {
        // Chip bị khóa RDP Level 1 hoặc 2 → KHÔNG THỂ nạp firmware
        // Phải unlock trước qua menu Tools → "Erase Chip (SWD)"
        ESP_LOGE(TAG, "FAIL: RDP Level %d — chip is protected!", rdp_level);
        ESP_LOGE(TAG, "      Run 'Erase STM32' to unlock first.");
        progress("RDP locked!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "      RDP Level 0 — OK");

    // ============================================================
    // BƯỚC 3/5: KẾT NỐI SWD CHO FLASH OPERATIONS
    // ============================================================
    // Đây là kết nối "thật" để thực hiện flash programming
    // Warm-up ở BƯỚC 2 chỉ là "thăm dò" → đã disconnect rồi
    // Bây giờ kết nối lại + giữ kết nối đến hết BƯỚC 5
    //
    // Sequence:
    // 1. flasher_swd_stm32f4_init(): Config GPIO SWDIO/SWCLK, init DAP object
    // 2. full_connect():
    //    - dap.dap_connect() → line reset + JTAG-to-SWD sequence
    //    - halt_core() → dừng CPU target (DHCSR + DEMCR)
    //    - dap.select() → SYSRESETREQ + re-halt + đọc chip info
    //    - Identify chip: IDCODE, DBGMCU, flash_size, device_name
    // 3. Validate: firmware_size ≤ flash_size
    progress("Connecting...", 5);
    ESP_LOGI(TAG, "[3/5] Connecting for flash...");

    ret = flasher_swd_stm32f4_init();
    if (ret != ESP_OK) {
        // Init SWD interface fail → có thể GPIO conflict hoặc DAP object corrupt
        progress("SWD init fail!", 0);
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    uint32_t idcode = 0, chip_id = 0;
    ret = full_connect(&idcode, &chip_id);
    if (ret != ESP_OK) {
        // Reconnect fail sau khi warm-up → hiếm gặp
        // Nguyên nhân có thể: target bị reset ngoài ý muốn, power drop, wiring loose
        ESP_LOGE(TAG, "FAIL: Reconnect failed");
        progress("Connect fail!", 0);
        flasher_swd_stm32f4_deinit();
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ret;
    }

    // --- Validate flash size vs firmware size ---
    // dap.target_device.flash_size được đọc từ FLASH_SIZE register (0x1FFF7A22)
    // Nếu register này = 0 hoặc 0xFFFF → chip không hỗ trợ auto-detect flash size
    uint32_t flash_size = dap.target_device.flash_size;
    if (flash_size > 0) {
        ESP_LOGI(TAG, "      Chip: %s | Flash: %" PRIu32 " KB | FW: %" PRIu32 " KB",
                 dap.target_device.name ? dap.target_device.name : "unknown",
                 flash_size / 1024, (uint32_t)(file_size / 1024));

        // Kiểm tra firmware có vừa vào flash không
        // Nếu firmware lớn hơn flash → dừng ngay, không nạp (tránh brick chip)
        if (file_size > flash_size) {
            ESP_LOGE(TAG, "FAIL: FW (%zu B) > flash (%" PRIu32 " B)!", file_size, flash_size);
            progress("FW too large!", 0);
            full_disconnect();
            flasher_swd_stm32f4_deinit();
            if (streaming) fclose(fwFile);
            free(fw_buf);
            return ESP_FAIL;
        }
    } else {
        // Flash size unknown → không thể validate
        // Tiếp tục nạp nhưng có rủi ro nếu firmware quá lớn
        ESP_LOGW(TAG, "      Chip: %s | Flash size: unknown",
                 dap.target_device.name ? dap.target_device.name : "unknown");
    }

    // --- Cấp phát buffer verify cho on-the-fly checking ---
    // Buffer 256B để đọc lại data sau khi ghi, so sánh với data gốc
    // Nếu không match → retry ngay (tối đa 3 lần)
    // Đây là buffer TẠM, dùng chung cho TẤT CẢ chunks (không free/realloc liên tục)
    uint8_t* verify_buf = (uint8_t*)malloc(SWD_PROGRAM_CHUNK);
    if (!verify_buf) {
        // Không cấp phát được 256B → hệ thống đang cực kỳ ít RAM
        ESP_LOGE(TAG, "FAIL: Cannot allocate verify buffer");
        full_disconnect();
        flasher_swd_stm32f4_deinit();
        if (streaming) fclose(fwFile);
        free(fw_buf);
        return ESP_ERR_NO_MEM;
    }

    // ============================================================
    // BƯỚC 4-5/5: ERASE → PROGRAM + VERIFY ON-THE-FLY → RETRY
    // ============================================================
    // Đây là phần QUAN TRỌNG NHẤT của toàn bộ hàm!
    //
    // Tại sao cần on-the-fly verify?
    // - ESP32-C3 bit-bang SWD không có hardware support
    // - dap_write_word() dùng GPIO toggle thủ công → tỷ lệ lỗi ~1/1000 words
    // - Lỗi này SILENT (không báo lỗi, ACK vẫn OK) → data ghi sai nhưng không biết
    // - Nếu verify SAU CÙNG (cuối file) → phát hiện lỗi quá muộn → phải xóa + nạp lại toàn bộ
    // - On-the-fly verify: ghi 256B → đọc lại ngay → so sánh → retry ngay nếu sai
    //   → Sửa lỗi TẠI CHỖ, không cần nạp lại toàn bộ file
    //
    // Chiến lược retry 2 tầng:
    // TẦNG 1: CHUNK-LEVEL RETRY (retry tại chỗ)
    // - Mỗi chunk 256B: program → verify → nếu sai → retry tối đa 3 lần
    // - 99% trường hợp: 1 retry đủ để sửa lỗi
    // - Nếu 3 lần vẫn fail → chunk này có vấn đề nghiêm trọng (SWD bus error, target hung)
    //   → Chuyển sang TẦNG 2
    //
    // TẦNG 2: FULL RE-FLASH (retry toàn bộ file)
    // - Disconnect → delay(50ms) → reconnect
    // - Erase LẠI toàn bộ sectors
    // - Nạp LẠI từ đầu file
    // - Tối đa 3 attempts (3 × erase + program + verify)
    // - Nếu 3 attempts vẫn fail → hardware issue nghiêm trọng:
    //   - SWD wiring không ổn định (loose connection, noise)
    //   - Target power không đủ (brown-out during flash)
    //   - Flash controller bị lỗi (hiếm gặp)
    //
    // Tại sao chunk size = 256B?
    // - STM32 flash programming: mỗi lần flash_unlock + set PG bit → ghi nhiều words → flash_lock
    // - Quá nhỏ (64B): nhiều lần unlock/lock → chậm, overhead cao
    // - Quá lớn (1KB): nếu 1 word fail → cả 1KB fail → retry mất thời gian
    // - 256B = sweet spot: 64 words, cân bằng giữa performance vs granularity
    //
    // Streaming mode handling:
    // - Full-RAM: seg_data = fw_buf + file_offset (pointer đến vị trí trong buffer)
    // - Streaming: đọc 32KB từ SD vào fw_buf → seg_data = fw_buf (toàn bộ buffer)
    //              → nạp xong 32KB → đọc 32KB tiếp theo → loop
    // - Cả 2 mode đều dùng CÙNG logic program + verify (code chung)

    // Tổng số chunks 256B cần nạp (làm tròn lên)
    // Ví dụ: 65KB firmware = 260 chunks, 256KB firmware = 1024 chunks
    int total_chunks = (padded_size + SWD_PROGRAM_CHUNK - 1) / SWD_PROGRAM_CHUNK;
    bool flash_ok = false;  // Flag: true = nạp thành công, false = cần retry

    // ============================================================
    // FULL RE-FLASH ATTEMPT LOOP (TẦNG 2 RETRY)
    // ============================================================
    // Outer loop: Thử nạp toàn bộ file tối đa 3 lần
    // Mỗi attempt = 1 chu kỳ hoàn chỉnh: disconnect → reconnect → erase → program → verify
    for (int attempt = 1; attempt <= SWD_MAX_ATTEMPTS; attempt++) {
        if (attempt > 1) {
            // Đây là lần retry (attempt 2 hoặc 3)
            // Attempt 1 đã FAIL → cần reset lại toàn bộ state

            ESP_LOGW(TAG, "=== RE-FLASH attempt %d/%d ===", attempt, SWD_MAX_ATTEMPTS);
            progress("Retry...", 3);

            // --- Reset SWD connection ---
            // Disconnect + deinit giải phóng tất cả state cũ
            // delay(50ms) cho target flash controller reset hoàn toàn
            full_disconnect();
            flasher_swd_stm32f4_deinit();
            delay(50);

            // Reconnect từ đầu (giống BƯỚC 3)
            ret = flasher_swd_stm32f4_init();
            if (ret != ESP_OK) {
                // Init fail → không thể retry nữa, cleanup + return
                free(verify_buf);
                if (streaming) fclose(fwFile);
                free(fw_buf);
                return ret;
            }

            ret = full_connect(&idcode, &chip_id);
            if (ret != ESP_OK) {
                // Reconnect fail → không thể retry, cleanup + return
                flasher_swd_stm32f4_deinit();
                free(verify_buf);
                if (streaming) fclose(fwFile);
                free(fw_buf);
                return ret;
            }

            // --- Streaming mode: reopen file ---
            // Tại sao phải reopen thay vì seek(0)?
            // - Arduino SD library có bug: seek(0) sau EOF không reliable
            // - Đôi khi seek(0) OK nhưng read() tiếp theo return 0 (EOF sticky)
            // - Close + reopen đảm bảo file state reset hoàn toàn
            if (streaming) {
                fclose(fwFile);
                fwFile = fopen(fw_full_path.c_str(), "rb");
                if (!fwFile) {
                    // Không mở lại được file → có thể SD unmount, file bị xóa
                    ESP_LOGE(TAG, "FAIL: Cannot reopen file for retry");
                    full_disconnect();
                    flasher_swd_stm32f4_deinit();
                    free(verify_buf);
                    free(fw_buf);
                    return ESP_FAIL;
                }
            }
        }

        // ============================================================
        // BƯỚC 4/5: XÓA FLASH (SECTOR ERASE)
        // ============================================================
        // program_start() xóa TẤT CẢ sectors cần thiết cho firmware
        //
        // Cách hoạt động:
        // - Tính toán sectors cần xóa dựa trên start_addr + size
        // - STM32F4 sector layout:
        //   Sector 0-3: 16KB mỗi sector (tổng 64KB)
        //   Sector 4:   64KB
        //   Sector 5-11: 128KB mỗi sector
        // - Ví dụ: firmware 65KB
        //   → Cần sectors 0-3 (64KB) + 1KB đầu Sector 4
        //   → Phải xóa TOÀN BỘ Sector 4 (64KB) vì không thể xóa một phần
        //   → Tổng xóa: 128KB (64KB + 64KB)
        //
        // CẢNH BÁO:
        // - Nếu config data nằm trong sectors bị xóa → SẼ MẤT!
        // - Target firmware developer phải thiết kế linker script đúng:
        //   - App firmware: ≤256KB (sectors 0-4)
        //   - Config data: từ sector 5+ (0x08020000 trở đi)
        // - FlashPorter không biết config data ở đâu → không thể protect
        //
        // Địa chỉ 0x08000000:
        // - Đây là địa chỉ FLASH TRỰC TIẾP, không phải alias 0x00000000
        // - 0x00 là alias động (phụ thuộc BOOT0 pin) → không reliable
        // - 0x08000000 luôn trỏ đúng vào flash, bất kể BOOT0
        progress("Erasing...", 8);
        ESP_LOGI(TAG, "[4/5] Erasing flash...");
        dap.program_start(0x08000000, file_size);
        ESP_LOGI(TAG, "      Erase OK");

        // ============================================================
        // BƯỚC 5/5: NẠP FIRMWARE + VERIFY ON-THE-FLY
        // ============================================================
        // Đây là vòng lặp chính để ghi + verify firmware
        // Cấu trúc 3 tầng lồng nhau:
        // - Tầng 1 (ngoài cùng): Full re-flash attempts (đã vào rồi)
        // - Tầng 2: Segments (32KB mỗi segment trong Streaming mode)
        // - Tầng 3: Chunks (256B mỗi chunk, đơn vị ghi nhỏ nhất)
        // - Tầng 4 (trong cùng): Chunk retries (3 lần retry cho mỗi chunk)
        ESP_LOGI(TAG, "[5/5] Flashing %" PRIu32 " KB (%d chunks x %dB)...",
                 (uint32_t)(file_size / 1024), total_chunks, SWD_PROGRAM_CHUNK);

        uint32_t addr = 0x08000000;
        size_t file_offset = 0;
        int chunk_num = 0;
        int retry_count = 0;
        int last_pct = -1;
        bool this_attempt_ok = true;

        while (file_offset < padded_size) {
            // --- Load segment ---
            size_t seg_size = padded_size - file_offset;
            if (seg_size > buf_size) seg_size = buf_size;

            uint8_t* seg_data;
            if (streaming) {
                // Read segment from SD (sequential reads, no seek needed)
                size_t to_read = seg_size;
                if (file_offset + to_read > file_size) {
                    to_read = (file_offset < file_size) ? (file_size - file_offset) : 0;
                }
                size_t nread = 0;
                while (nread < to_read) {
                    size_t n = fread(fw_buf + nread, 1, to_read - nread, fwFile);
                    if (n == 0) break;
                    nread += n;
                }
                if (nread < to_read) {
                    ESP_LOGE(TAG, "SD read error at offset %zu (got %zu / %zu)",
                             file_offset, nread, to_read);
                    this_attempt_ok = false;
                    break;
                }
                // Pad remainder with 0xFF (alignment at end of file)
                if (seg_size > to_read) {
                    memset(fw_buf + to_read, 0xFF, seg_size - to_read);
                }
                seg_data = fw_buf;
            } else {
                seg_data = fw_buf + file_offset;
            }

            // --- Flash 256B chunks within segment ---
            size_t seg_offset = 0;
            while (seg_offset < seg_size) {
                size_t chunk = seg_size - seg_offset;
                if (chunk > SWD_PROGRAM_CHUNK) chunk = SWD_PROGRAM_CHUNK;
                chunk_num++;

                bool chunk_ok = false;
                for (int cr = 0; cr < SWD_CHUNK_RETRIES; cr++) {
                    dap.programBlock(addr, seg_data + seg_offset, chunk);
                    vTaskDelay(1);
                    bool read_ok = dap.dap_read_block(addr, verify_buf, chunk);

                    if (read_ok && memcmp(seg_data + seg_offset, verify_buf, chunk) == 0) {
                        chunk_ok = true;
                        break;
                    }

                    retry_count++;
                    if (read_ok) {
                        for (size_t i = 0; i < chunk; i += 4) {
                            uint32_t exp_w = *(const uint32_t*)(seg_data + seg_offset + i);
                            uint32_t got_w = *(const uint32_t*)(verify_buf + i);
                            if (exp_w != got_w) {
                                ESP_LOGW(TAG, "      [%d/%d] MISMATCH @0x%08" PRIx32
                                         " exp=0x%08" PRIx32 " got=0x%08" PRIx32 " retry %d/%d",
                                         chunk_num, total_chunks, addr + (uint32_t)i,
                                         exp_w, got_w, cr + 1, SWD_CHUNK_RETRIES);
                                break;
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "      [%d/%d] READ FAIL @0x%08" PRIx32 " retry %d/%d",
                                 chunk_num, total_chunks, addr, cr + 1, SWD_CHUNK_RETRIES);
                    }
                }

                if (!chunk_ok) {
                    ESP_LOGE(TAG, "      [%d/%d] FAILED after %d retries — need full re-erase",
                             chunk_num, total_chunks, SWD_CHUNK_RETRIES);
                    this_attempt_ok = false;
                    break;
                }

                addr += chunk;
                seg_offset += chunk;

                // Progress: 8% (erase) → 95% (flash done)
                size_t global_offset = file_offset + seg_offset;
                int pct = 8 + (int)((uint64_t)global_offset * 87 / padded_size);
                if (pct != last_pct) {
                    last_pct = pct;
                    char prog_text[32];
                    snprintf(prog_text, sizeof(prog_text), "Flash %d/%d", chunk_num, total_chunks);
                    progress(prog_text, pct);
                }
            }

            if (!this_attempt_ok) break;
            file_offset += seg_size;
        }

        if (this_attempt_ok) {
            if (retry_count > 0) {
                ESP_LOGI(TAG, "      DONE! %d/%d chunks OK (%d retries recovered)",
                         total_chunks, total_chunks, retry_count);
            } else {
                ESP_LOGI(TAG, "      DONE! %d/%d chunks OK (no retries needed)",
                         total_chunks, total_chunks);
            }
            flash_ok = true;
            break;
        }

        ESP_LOGW(TAG, "      Attempt %d/%d FAILED — will re-erase and retry",
                 attempt, SWD_MAX_ATTEMPTS);
    }

    free(verify_buf);
    if (streaming) fclose(fwFile);

    if (!flash_ok) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "  FLASH FAILED after %d attempts!", SWD_MAX_ATTEMPTS);
        ESP_LOGE(TAG, "  Check SWD wiring & target power");
        ESP_LOGE(TAG, "========================================");
        progress("Flash FAILED!", 0);
        full_disconnect();
        flasher_swd_stm32f4_deinit();
        free(fw_buf);
        return ESP_FAIL;
    }

    // --- Cleanup ---
    progress("Done!", 100);
    full_disconnect();
    flasher_swd_stm32f4_deinit();
    free(fw_buf);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FLASH OK! %" PRIu32 " KB written", (uint32_t)(file_size / 1024));
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}
