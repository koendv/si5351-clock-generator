/*
 * si5351mcu - Si5351 library for Arduino MCU tuned for size and click-less
 *
 * Copyright (C) 2017 Pavel Milanes <pavelmc@gmail.com>
 *
 * Many chunk of codes are derived-from/copied from other libs
 * all GNU GPL licenced:
 *  - Linux Kernel (www.kernel.org)
 *  - Hans Summers libs and demo code (qrp-labs.com)
 *  - Etherkit (NT7S) Si5351 libs on github
 *  - DK7IH example.
 *  - Jerry Gaffke integer routines for the bitx20 group
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/****************************************************************************
 *  This lib tricks:
 *
 * CLK0 will use PLLA
 * CLK1 will use PLLB
 * CLK2 will use PLLB
 *
 * Lib defaults
 * - XTAL is 27 Mhz.
 * - Always put the internal 8pF across the xtal legs to GND
 * - lowest power output (2mA)
 * - After the init all outputs are off, you need to enable them in your code.
 *
 * The correction procedure is not for the PPM as other libs, this
 * is just +/- Hz to the XTAL freq, you may get a click noise after
 * applying a correction
 *
 * The init procedure is mandatory as it set the Xtal (the default or a custom
 * one) and prepare the Wire (I2C) library for operation.
 ****************************************************************************/

#ifndef SI5351MCU_H
#define SI5351MCU_H

// rigor includes
#include <Arduino.h>
#include <SoftI2C.h>

#define SI5351MCU_VERSION "0.7.1"

// default frequency of Si5351 crystal
#define SI_XTAL 27000000L

// default I2C address of the Si5351A - other variants may differ
#define SIADDR 0x60

// The number of output channels - 3 for Si5351A 10 pin
#define SICHANNELS 3

// register's power modifiers
#define SIOUT_2mA 0
#define SIOUT_4mA 1
#define SIOUT_6mA 2
#define SIOUT_8mA 3

// registers base (2mA by default)
#define SICLK0_R 76    // 0b01001100
#define SICLK12_R 108  // 0b01101100

// base xtal freq, over this we apply the correction factor
// by default 27 MHz
extern uint32_t base_xtal;

// this is the work value, with the correction applied
// via the correction() procedure
extern uint32_t int_xtal;

// clk# power holders (2ma by default)
extern uint8_t clkpower[SICHANNELS];

// local var to keep track of when to reset the "pll"
/*********************************************************
 * BAD CONCEPT on the datasheet and AN:
 *
 * The chip has a soft-reset for PLL A & B but in
 * practice the PLL does not need to be resetted.
 *
 * Test shows that if you fix the Msynth output
 * dividers and move any of the VCO from bottom to top
 * the frequency moves smooth and clean, no reset needed
 *
 * The reset is needed when you changes the value of the
 * Msynth output divider, even so it's not always needed
 * so we use this var to keep track of all three and only
 * reset the "PLL" when this value changes to be sure
 *
 * It's a word (16 bit) because the final max value is 900
 *********************************************************/
extern uint16_t omsynth[SICHANNELS];
extern uint8_t o_Rdiv[SICHANNELS];

// var to check the clock state
extern bool clkOn[SICHANNELS];  // This should not really be public - use isEnabled()

// default init procedure
void si5351mcu_default_init(void);

// custom init procedure (XTAL in Hz);
void si5351mcu_init(uint8_t, uint32_t);

// reset all PLLs
static void si5351mcu_reset(void);

// set CLKx(0..2) to freq (Hz)
void si5351mcu_setFreq(uint8_t, uint32_t);

// pass a correction factor
void si5351mcu_correction(int32_t);

// enable some CLKx output
void si5351mcu_enable(uint8_t);

// disable some CLKx output
void si5351mcu_disable(uint8_t);

// disable all outputs
void si5351mcu_off(void);

// set power output to a specific clk
void si5351mcu_setPower(uint8_t, uint8_t);

// check if si5351 ready
bool si5351mcu_not_ready();

// used to talk with the chip, via Arduino Wire lib
//

void i2cWrite(const uint8_t reg, const uint8_t val);
uint8_t i2cWriteBurst(const uint8_t start_register, const uint8_t *data, const uint8_t numbytes);
int16_t i2cRead(const uint8_t reg);

inline const bool si5351mcu_isEnabled(const uint8_t channel) {
  return channel < SICHANNELS && clkOn[channel] != 0;
}

inline const uint8_t si5351mcu_getPower(const uint8_t channel) {
  return channel < SICHANNELS ? clkpower[channel] : 0;
}

inline const uint32_t si5351mcu_getXtalBase(void) {
  return base_xtal;
}

inline const uint32_t si5351mcu_getXtalCurrent(void) {
  return int_xtal;
}

#endif  //SI5351MCU_H
