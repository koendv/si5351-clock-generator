/*
 * Clock Generator 8 kHz - 200 MHz.
 * SI5351 or MS5351M.
 *
 * compile with ch55xduino for wch ch552e
 * https://github.com/DeqingSun/ch55xduino
 * in Arduino IDE Tools -> Settings:
 * Board: CH552 board 
 * Clock Source: 16 MHz Internal, 3.3V or 5V
 * Bootloader Pin: P3.6 (D+) pull-up
 * Upload method: USB
 * USB Settings: Default CDC
 */

#include <SoftI2C.h>
#include "si5351.h"

#define F_XTAL 26000000UL  // crystal frequency
#define F_MAX 200000000UL
#define F_MIN 7810UL
#define F_DEFAULT 10000000UL
#define CRYSTAL_CL SI5351_CRYSTAL_LOAD_0PF
#define POWER_MAX 3
#define POWER_DEFAULT 3
#define SETTINGS_MAGIC 0xC0FFEEUL
#define LED_BUILTIN 15

void PrintHelp();
void PrintFrequency(uint32_t f_out);
void StoreSettings();
void RecallSettings();
void BlinkLed();
void SelfTest();
void SetFrequency(uint32_t frequency);
void SetPower(byte power);
void Si5351a_Init();
void Si5351a_Check();
void Si5351a_Write_Reg(byte reg, byte value);
byte Si5351a_Read_Reg(byte reg);

struct settings_struct {
  uint32_t f;
  uint32_t magic;
  byte power;
};

struct settings_struct settings;

bool print_banner = true;
bool error_flag = false;
uint32_t led_millis = 0;

char line_buf[10] = { 0 };  // character buffer to enter frequency
byte line_len = 0;

void LedOn() {
  digitalWrite(LED_BUILTIN, HIGH);
}

void LedOff() {
  digitalWrite(LED_BUILTIN, LOW);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  LedOn();
  RecallSettings();
  delay(100);
  LedOff();
  I2CInit();     // Initialize I2C-communication as master
  sda_pin = 16;  // SDA on pin P1.6
  scl_pin = 17;  // SCL on pin P1.7
  Si5351a_Init();
  SetFrequency(settings.f);  // Set output frequency
  SetPower(settings.power);  // Switch output on
}

void loop() {
  if (USBSerial_available()) {
    if (print_banner) {
      USBSerial_print("\r\nsi5351 clock gen\r\n? for help\r\n>");
      print_banner = false;
    }
    char ch = USBSerial_read();
    if ((ch >= 'A') && (ch <= 'Z')) ch += 'a' - 'A';  // to lower case
    switch (ch) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (line_len < sizeof(line_buf)) {
          USBSerial_write(ch);
          line_buf[line_len++] = ch;
        }
        break;
      case '\b':
        if (line_len > 0) {
          --line_len;
          USBSerial_print("\b \b");
        }
        break;
      case '\r':
      case '\n':
      case 'k':
      case 'm':  // set new frequency
        if (line_len > 0) {
          // convert to int
          uint32_t new_f = 0;
          for (uint8_t i = 0; i < line_len; i++)
            new_f = new_f * 10 + line_buf[i] - '0';
          // check bounds
          if (ch == 'k') {
            if (new_f <= F_MAX / 1000) new_f *= 1000;  // kHz
            else new_f = F_MAX;
          }
          if (ch == 'm') {
            if (new_f <= F_MAX / 1000000) new_f *= 1000000;  // MHz
            else new_f = F_MAX;
          }
          if (new_f < F_MIN) new_f = F_MIN;
          if (new_f > F_MAX) new_f = F_MAX;
          settings.f = new_f;
          SetFrequency(settings.f);
          SetPower(settings.power);
          line_len = 0;
        }
        break;
      case '+':  // more power
        if (settings.power < POWER_MAX) {
          settings.power++;
          SetPower(settings.power);
          line_len = 0;
        }
        break;
      case '-':  // less power
        if (settings.power > 0) {
          --settings.power;
          SetPower(settings.power);
          line_len = 0;
        }
        break;
      case 's':  // store settings
        StoreSettings();
        USBSerial_print("store");
        line_len = 0;
        break;
      case 'r':  // recall settings
        RecallSettings();
        SetFrequency(settings.f);
        SetPower(settings.power);
        USBSerial_print("recall");
        line_len = 0;
        break;
      case 't':  // frequency sweep
        SelfTest();
        break;
      case '?':  // help
      case 'h':
        PrintHelp();
        line_len = 0;
        break;
      default:
        break;
    }

    if (line_len == 0) {  // not entering a frequency
      USBSerial_println();
      PrintFrequency(settings.f);
      USBSerial_print(" ");
      USBSerial_print((char)('2' + 2 * settings.power));  // '2', '4', '6' or '8'
      USBSerial_println("mA\r\n>");
    }
    USBSerial_flush();
    BlinkLed();
  }
}

void PrintHelp() {
  USBSerial_print("\r\ncompiled " __DATE__ "\r\ncrystal ");
  USBSerial_print(F_XTAL);
  USBSerial_print(
    " Hz\r\n"
    "0..9,backspace: frequency\r\n"
    "enter: set Hz\r\n"
    "k: set kHz\r\n"
    "m: set MHz\r\n"
    "+,-: power up/down\r\n"
    "s: store\r\n"
    "r: recall\r\n"
    "t: self-test\r\n"
    "?,h: help\r\n");
}

void PrintFrequency(uint32_t f_out) {
  char prefix = ' ';
  if (f_out % 1000000 == 0) {
    f_out /= 1000000;
    prefix = 'M';
  } else if (f_out % 1000 == 0) {
    f_out /= 1000;
    prefix = 'k';
  }
  USBSerial_print(f_out);
  USBSerial_print(prefix);
  USBSerial_print("Hz");
}

void StoreSettings() {  // store frequency and power in non-volatile memory
  uint8_t i;
  char *s = (char *)&settings;
  for (i = 0; i < sizeof(settings); i++)
    eeprom_write_byte(i, s[i]);
}

void RecallSettings() {  // recall frequency and power from non-volatile memory
  char *s = (char *)&settings;
  for (uint8_t i = 0; i < sizeof(settings); i++)
    s[i] = eeprom_read_byte(i);
  bool valid = (settings.magic == SETTINGS_MAGIC) && (settings.f >= F_MIN) && (settings.f <= F_MAX) && (settings.power <= POWER_MAX);
  if (!valid) {  // first run, set default values
    settings.f = F_DEFAULT;
    settings.power = POWER_DEFAULT;
    settings.magic = SETTINGS_MAGIC;
    StoreSettings();
  }
}

/* sweep through frequency range */

void SelfTest() {
  error_flag = false;
  uint32_t f = 8000;
  while (f <= F_MAX) {
    PrintFrequency(f);
    USBSerial_println();
    USBSerial_flush();
    SetFrequency(f);
    SetPower(settings.power);
    delay(3000);
    Si5351a_Check();
    if (error_flag) LedOn();
    if (f < 10000) f += 1000;
    else if (f < 100000) f += 10000;
    else if (f < 1000000) f += 100000;
    else if (f < 10000000) f += 1000000;
    else f += 10000000;
  }
  USBSerial_println(error_flag ? "failed" : "ok");
  SetFrequency(settings.f);
  SetPower(settings.power);
}

void BlinkLed() {  // blink led 5 seconds if an error occurs
  uint32_t current_millis = millis();
  if (current_millis - led_millis > 5000) {
    Si5351a_Check();
    if (error_flag) {
      LedOn();
      error_flag = false;
    } else
      LedOff();
    led_millis = current_millis;
  }
}

/*
 * After https://www.qrp-labs.com/synth/oe1cgs.html
 * Note: the use of floating point can be avoided with the continued fraction algorithm.
 */

void SetFrequency(uint32_t frequency) {  // Frequency in Hz; must be within [7810 Hz to 200 Mhz]
  uint32_t fvco;                         // VCO frequency (600-900 MHz) of PLL
  uint32_t outdivider;                   // Output divider in range [4,6,8-900], even numbers preferred
  byte R = 1;                            // Additional Output Divider in range [1,2,4,...128]
  byte a;                                // "a" part of Feedback-Multiplier from XTAL to PLL in range [15,90]
  uint32_t b;                            // "b" part of Feedback-Multiplier from XTAL to PLL
  const uint32_t c = 0xFFFFEUL;          // "c" part of Feedback-Multiplier from XTAL to PLL
  float f;                               // auxiliary variable
  uint32_t MS0_P1;                       // Si5351a Output Divider register MS0_P1, P2 and P3 are hardcoded below
  uint32_t MSNA_P1;                      // Si5351a Feedback Multisynth register MSNA_P1
  uint32_t MSNA_P2;                      // Si5351a Feedback Multisynth register MSNA_P2
  uint32_t MSNA_P3;                      // Si5351a Feedback Multisynth register MSNA_P3
                                         //
  outdivider = 900000000 / frequency;    // 900 MHz is the maximum internal PLL-Frequency
  while (outdivider > 900) {             // If output divider out of range (>900) use additional Output divider
    R = R * 2;                           //
    outdivider = outdivider / 2;         //
  }                                      //
  if (outdivider % 2) outdivider--;      // finds the even divider which delivers the intended Frequency
  fvco = outdivider * R * frequency;     // Calculate the PLL-Frequency (given the even divider)
  switch (R) {                           // Convert the Output Divider to the bit-setting required in register 44
    case 1: R = 0; break;                // Bits [6:4] = 000
    case 2: R = 16; break;               // Bits [6:4] = 001
    case 4: R = 32; break;               // Bits [6:4] = 010
    case 8: R = 48; break;               // Bits [6:4] = 011
    case 16: R = 64; break;              // Bits [6:4] = 100
    case 32: R = 80; break;              // Bits [6:4] = 101
    case 64: R = 96; break;              // Bits [6:4] = 110
    case 128: R = 112; break;            // Bits [6:4] = 111
  }                                      //
  a = fvco / F_XTAL;                     // Multiplier to get from crystal freq to PLL-freq.
  f = fvco - a * F_XTAL;                 // Multiplier = a+b/c
  f = f * c;                             // this is just mathematics
  f = f / F_XTAL;                        //
  b = f;                                 //
  MS0_P1 = 128 * outdivider - 512;       // Calculation of Output Divider registers MS0_P1 to MS0_P3
                                         // MS0_P2 = 0 and MS0_P3 = 1; these values are hardcoded, see below
  f = 128UL * b / c;                     // Calculation of Feedback Multisynth registers MSNA_P1 to MSNA_P3
  MSNA_P1 = 128 * a + f - 512;
  MSNA_P2 = f;
  MSNA_P2 = 128 * b - MSNA_P2 * c;
  MSNA_P3 = c;
  Si5351a_Write_Reg(SI5351_CLK0_CTRL, SI5351_CLK_POWERDOWN);                                                 // Disable output when changing registers
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS, (MSNA_P3 & 0xff00) >> 8);                                        // Bits [15:8] of MSNA_P3 in register 26
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 1, MSNA_P3 & 0xff);                                             // Bits [7:0]  of MSNA_P3 in register 27
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 2, (MSNA_P1 & 0x030000) >> 16);                                 // Bits [17:16] of MSNA_P1 in bits [1:0] of register 28
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 3, (MSNA_P1 & 0xff00) >> 8);                                    // Bits [15:8]  of MSNA_P1 in register 29
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 4, MSNA_P1 & 0xff);                                             // Bits [7:0]  of MSNA_P1 in register 30
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 5, ((MSNA_P3 & 0xf0000) >> 12) | ((MSNA_P2 & 0xf0000) >> 16));  // Parts of MSNA_P3 und MSNA_P1
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 6, (MSNA_P2 & 0xff00) >> 8);                                    // Bits [15:8]  of MSNA_P2 in register 32
  Si5351a_Write_Reg(SI5351_PLLA_PARAMETERS + 7, MSNA_P2 & 0xff);                                             // Bits [7:0]  of MSNA_P2 in register 33
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS, 0);                                                              // Bits [15:8] of MS0_P3 (always 0) in register 42
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 1, 1);                                                          // Bits [7:0]  of MS0_P3 (always 1) in register 43
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 2, ((MS0_P1 & 0x30000) >> 16) | R);                             // Bits [17:16] of MS0_P1 in bits [1:0] and R in [7:4]
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 3, (MS0_P1 & 0xff00) >> 8);                                     // Bits [15:8]  of MS0_P1 in register 45
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 4, MS0_P1 & 0xff);                                              // Bits [7:0]  of MS0_P1 in register 46
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 5, 0);                                                          // Bits [19:16] of MS0_P2 and MS0_P3 are always 0
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 6, 0);                                                          // Bits [15:8]  of MS0_P2 are always 0
  Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 7, 0);                                                          // Bits [7:0]   of MS0_P2 are always 0
  if (outdivider == 4) {
    Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 2, 0xc | R);  // Special settings for R = 4 (see datasheet)
    Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 3, 0);        // Bits [15:8]  of MS0_P1 must be 0
    Si5351a_Write_Reg(SI5351_CLK0_PARAMETERS + 4, 0);        // Bits [7:0]  of MS0_P1 must be 0
  }
  Si5351a_Write_Reg(SI5351_PLL_RESET, SI5351_PLL_RESET_A);  // reset PLL A
}

void SetPower(byte power) {  // Sets the output power level
  if (power <= POWER_MAX) {  // valid power values are 0 (2mA), 1 (4mA), 2 (6mA) or 3 (8mA into 50 ohm load)
    Si5351a_Write_Reg(SI5351_CLK0_CTRL, SI5351_CLK_INTEGER_MODE | SI5351_CLK_INPUT_MULTISYNTH_N | power);
  }
}

void Si5351a_Init() {  // initialize si5351
  uint8_t status = 0;
  do {
    status = Si5351a_Read_Reg(SI5351_DEVICE_STATUS);
  } while (status & SI5351_STATUS_SYS_INIT);  // wait until chip initialized
  Si5351a_Write_Reg(SI5351_CRYSTAL_LOAD, CRYSTAL_CL | 0b00010010);
  for (uint8_t i = 0; i <= 7; i++)
    Si5351a_Write_Reg(SI5351_CLK0_CTRL + i, SI5351_CLK_POWERDOWN);
}

void Si5351a_Check() {  // set 'error_flag' if si5351 loses lock
  uint8_t status = Si5351a_Read_Reg(SI5351_DEVICE_STATUS);
  if (status & (SI5351_STATUS_LOL_A | SI5351_STATUS_LOS_XTAL)) error_flag = true;
}

void Si5351a_Write_Reg(byte reg, byte value) {  // Writes "value" into "reg" of Si5351a via I2C
  uint8_t ack_bit;
  I2CStart();
  ack_bit = I2CSend(SI5351_BUS_BASE_ADDR << 1 | 0);
  if (ack_bit == 0) ack_bit = I2CSend(reg);
  if (ack_bit == 0) ack_bit = I2CSend(value);
  I2CStop();
  if (ack_bit != 0) error_flag = true;
}

byte Si5351a_Read_Reg(byte reg) {  // Read "reg" of Si5351a via I2C
  uint8_t ack_bit;
  byte value = 0;
  I2CStart();
  ack_bit = I2CSend(SI5351_BUS_BASE_ADDR << 1 | 0);
  if (ack_bit == 0) ack_bit = I2CSend(reg);
  I2CStop();
  if (ack_bit == 0) {
    I2CStart();
    ack_bit = I2CSend(SI5351_BUS_BASE_ADDR << 1 | 1);
    if (ack_bit == 0) value = I2CRead();
    I2CNak();
    I2CStop();
  }
  if (ack_bit != 0) error_flag = true;
  return value;
}

// not truncated
