/*!
 * @file Adafruit_SH1106G.cpp
 *
 */

#include "Adafruit_SH110X.h"
#include "splash.h"

// CONSTRUCTORS, DESTRUCTOR ------------------------------------------------

/*!
    @brief  Constructor for I2C-interfaced SH1106G displays.
    @param  w
            Display width in pixels
    @param  h
            Display height in pixels
    @param  twi
            Pointer to an existing TwoWire instance (e.g. &Wire, the
            microcontroller's primary I2C bus).
    @param  rst_pin
            Reset pin (using Arduino pin numbering), or -1 if not used
            (some displays might be wired to share the microcontroller's
            reset pin).
    @param  clkDuring
            Speed (in Hz) for Wire transmissions in SH110X library calls.
            Defaults to 400000 (400 KHz), a known 'safe' value for most
            microcontrollers, and meets the SH110X datasheet spec.
            Some systems can operate I2C faster (800 KHz for ESP32, 1 MHz
            for many other 32-bit MCUs), and some (perhaps not all)
            SH110X's can work with this -- so it's optionally be specified
            here and is not a default behavior. (Ignored if using pre-1.5.7
            Arduino software, which operates I2C at a fixed 100 KHz.)
    @param  clkAfter
            Speed (in Hz) for Wire transmissions following SH110X library
            calls. Defaults to 100000 (100 KHz), the default Arduino Wire
            speed. This is done rather than leaving it at the 'during' speed
            because other devices on the I2C bus might not be compatible
            with the faster rate. (Ignored if using pre-1.5.7 Arduino
            software, which operates I2C at a fixed 100 KHz.)
    @note   Call the object's begin() function before use -- buffer
            allocation is performed there!
*/
Adafruit_SH1106G::Adafruit_SH1106G(uint16_t w, uint16_t h, TwoWire *twi,
                                   int16_t rst_pin, uint32_t clkDuring,
                                   uint32_t clkAfter)
    : Adafruit_SH110X(w, h, twi, rst_pin, clkDuring, clkAfter) {}

/*!
    @brief  Constructor for SPI SH1106G displays, using software (bitbang)
            SPI.
    @param  w
            Display width in pixels
    @param  h
            Display height in pixels
    @param  mosi_pin
            MOSI (master out, slave in) pin (using Arduino pin numbering).
            This transfers serial data from microcontroller to display.
    @param  sclk_pin
            SCLK (serial clock) pin (using Arduino pin numbering).
            This clocks each bit from MOSI.
    @param  dc_pin
            Data/command pin (using Arduino pin numbering), selects whether
            display is receiving commands (low) or data (high).
    @param  rst_pin
            Reset pin (using Arduino pin numbering), or -1 if not used
            (some displays might be wired to share the microcontroller's
            reset pin).
    @param  cs_pin
            Chip-select pin (using Arduino pin numbering) for sharing the
            bus with other devices. Active low.
    @note   Call the object's begin() function before use -- buffer
            allocation is performed there!
*/
Adafruit_SH1106G::Adafruit_SH1106G(uint16_t w, uint16_t h, int16_t mosi_pin,
                                   int16_t sclk_pin, int16_t dc_pin,
                                   int16_t rst_pin, int16_t cs_pin)
    : Adafruit_SH110X(w, h, mosi_pin, sclk_pin, dc_pin, rst_pin, cs_pin) {}

/*!
    @brief  Constructor for SPI SH1106G displays, using native hardware SPI.
    @param  w
            Display width in pixels
    @param  h
            Display height in pixels
    @param  spi
            Pointer to an existing SPIClass instance (e.g. &SPI, the
            microcontroller's primary SPI bus).
    @param  dc_pin
            Data/command pin (using Arduino pin numbering), selects whether
            display is receiving commands (low) or data (high).
    @param  rst_pin
            Reset pin (using Arduino pin numbering), or -1 if not used
            (some displays might be wired to share the microcontroller's
            reset pin).
    @param  cs_pin
            Chip-select pin (using Arduino pin numbering) for sharing the
            bus with other devices. Active low.
    @param  bitrate
            SPI clock rate for transfers to this display. Default if
            unspecified is 8000000UL (8 MHz).
    @note   Call the object's begin() function before use -- buffer
            allocation is performed there!
*/
Adafruit_SH1106G::Adafruit_SH1106G(uint16_t w, uint16_t h, SPIClass *spi,
                                   int16_t dc_pin, int16_t rst_pin,
                                   int16_t cs_pin, uint32_t bitrate)
    : Adafruit_SH110X(w, h, spi, dc_pin, rst_pin, cs_pin, bitrate) {}

/*!
    @brief  Destructor for Adafruit_SH1106G object.
*/
Adafruit_SH1106G::~Adafruit_SH1106G(void) {}

/*!
    @brief  Allocate RAM for image buffer, initialize peripherals and pins.
    @param  addr
            I2C address of corresponding SH110X display (or pass 0 to use
            default of 0x3C for 128x32 display, 0x3D for all others).
            SPI displays (hardware or software) do not use addresses, but
            this argument is still required (pass 0 or any value really,
            it will simply be ignored). Default if unspecified is 0.
    @param  reset
            If true, and if the reset pin passed to the constructor is
            valid, a hard reset will be performed before initializing the
            display. If using multiple SH110X displays on the same bus, and
            if they all share the same reset pin, you should only pass true
            on the first display being initialized, false on all others,
            else the already-initialized displays would be reset. Default if
            unspecified is true.
    @return true on successful allocation/init, false otherwise.
            Well-behaved code should check the return value before
            proceeding.
    @note   MUST call this function before any drawing or updates!
*/
bool Adafruit_SH1106G::begin(uint8_t addr, bool reset) {

  Adafruit_GrayOLED::_init(addr, reset);

  // NOTE: the physical panel on this board is an SSD1306, NOT an SH1106.
  // SSD1306 maps its 128 columns starting at column 0 (no 132-col RAM), so it
  // needs NO offset — the old value 2 was pushing the whole image 2px to the
  // right. See the SSD1306-style init sequence below (charge pump 0x8D,0x14)
  // which also fixes the dim/low-brightness look.
  _page_start_offset = 0;

#ifndef SH110X_NO_SPLASH
  drawBitmap((WIDTH - splash2_width) / 2, (HEIGHT - splash2_height) / 2,
             splash2_data, splash2_width, splash2_height, 1);
#endif

  // SSD1306 128x64 init (this panel is an SSD1306). PAGE addressing mode is
  // used so the base-class display() — which writes each page with the page +
  // column-set commands — keeps working unchanged. Must stay under 32 bytes.
  // clang-format off
  static const uint8_t init[] = {
      0xAE,        // display OFF
      0xD5, 0x80,  // clock divide / osc freq
      0xA8, 0x3F,  // multiplex ratio = 64
      0xD3, 0x00,  // display offset = 0
      0x40,        // start line = 0
      0x8D, 0x14,  // charge pump ON (internal VCC) — this is what fixes brightness
      0x20, 0x02,  // memory addressing mode = PAGE
      0xA1,        // segment remap (col 127 -> SEG0)
      0xC8,        // COM scan direction = remapped (flip vertical)
      0xDA, 0x12,  // COM pins hardware config
      0x81, 0xFF,  // contrast = max (brightest)
      0xD9, 0xF1,  // pre-charge period (internal VCC)
      0xDB, 0x40,  // VCOMH deselect level
      0xA4,        // resume to RAM content
      0xA6,        // normal (non-inverted) display
  };
  // clang-format on

  if (!oled_commandList(init, sizeof(init))) {
    return false;
  }

  delay(100);                     // 100ms delay recommended
  oled_command(SH110X_DISPLAYON); // 0xaf

  return true; // Success
}
