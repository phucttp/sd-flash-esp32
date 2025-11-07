// menu.h
#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- CẤU HÌNH CỨNG (Không đổi) ---
#define BTN_UP    21
#define BTN_DOWN  20
#define BTN_OK    10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C

// --- KHAI BÁO CÁC HÀM CÔNG CỘNG ---

/**
 * @brief Khởi tạo module menu.
 * @param disp         Đối tượng Adafruit_SSD1306
 * @param displayItems Mảng chuỗi để *hiển thị* (ví dụ: "1. Start")
 * @param idItems      Mảng chuỗi là ID tương ứng (ví dụ: "FW_001.bin")
 * @param len          Số lượng mục (phải giống nhau cho cả 2 mảng)
 */
void menu_init(Adafruit_SSD1306& disp, const char* displayItems[], const char* idItems[], int len);

/**
 * @brief Cập nhật trạng thái menu, kiểm tra nút nhấn.
 * @return  Trả về index của mục được chọn (0, 1, 2...).
 * @return  Trả về -1 nếu không có mục nào được chọn.
 */
int menu_update();

/**
 * @brief (MỚI) Lấy chuỗi ID từ index đã chọn.
 * @param index Index nhận được từ hàm menu_update().
 * @return const char* (chuỗi ID, ví dụ "FW_001.bin").
 * @return NULL nếu index không hợp lệ.
 */
const char* menu_get_id(int index);

/**
 * @brief Hiển thị màn hình thông báo "Selected".
 * @param index Index của mục vừa được chọn (lấy từ menu_update()).
 */
void menu_display_selection(int index);

/**
 * @brief Vẽ lại menu. 
 */
void menu_redisplay();

/**
 * @brief Hàm helper để hiển thị thông báo nhanh ra OLED
 */
void oled_show_message(const char* line1, const char* line2 = "");

#endif // MENU_H