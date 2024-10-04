//
// LED Matrix driver
// Simple LED Matrix driver for the Raspberry Pi PICO-W board
// Kamal Pillai
// 02/21/2023

#ifndef __LED_MAT_H
#define __LED_MAT_H

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "font.h"

// These map the GPIO pins to the HUB75 ports
// Color data can be any pin, must be within a contiguous 8
static const uint8_t HUB75_R0 = 2;
static const uint8_t HUB75_G0 = 3;
static const uint8_t HUB75_B0 = 4;
static const uint8_t HUB75_R1 = 5;
static const uint8_t HUB75_G1 = 8;
static const uint8_t HUB75_B1 = 9;
// Address pins can be, but a given An pin, the pin must >= n
// (ie, A1 cannot be pin 0, A2 cannot be pin 0 or 1, etc)
// This requirement can be removed, but requires an additional shift
// per address bit in the row_set_mask/row_clr_mask fuctions
static const uint8_t HUB75_A4 = 22;
static const uint8_t HUB75_A0 = 10;
static const uint8_t HUB75_A1 = 16;
static const uint8_t HUB75_A2 = 18;
static const uint8_t HUB75_A3 = 20;
// CLK can be any pin
static const uint8_t HUB75_CLK = 11;
// Latch and Blank must be consecutive pins, with Latch beign first
// Blank can be first, but requires some changes in the sm_ctrl pio program
static const uint8_t HUB75_LATCH = 12;
static const uint8_t HUB75_BLANK = 13;

// find minimum address pin
static const uint8_t HUB75_A0A1_BASE = (HUB75_A1 < HUB75_A0) ? HUB75_A1 : HUB75_A0;
static const uint8_t HUB75_A2A3_BASE = (HUB75_A3 < HUB75_A2) ? HUB75_A3 : HUB75_A2;
static const uint8_t HUB75_A0123_BASE = (HUB75_A2A3_BASE < HUB75_A0A1_BASE) ? HUB75_A2A3_BASE : HUB75_A0A1_BASE;
static const uint8_t HUB75_ADDR_BASE = (HUB75_A4 < HUB75_A0123_BASE) ? HUB75_A4 : HUB75_A0123_BASE;

// find maximum address pin
static const uint8_t HUB75_A0A1_MAX = (HUB75_A1 > HUB75_A0) ? HUB75_A1 : HUB75_A0;
static const uint8_t HUB75_A2A3_MAX = (HUB75_A3 > HUB75_A2) ? HUB75_A3 : HUB75_A2;
static const uint8_t HUB75_A0123_MAX = (HUB75_A2A3_MAX > HUB75_A0A1_MAX) ? HUB75_A2A3_MAX : HUB75_A0A1_MAX;
static const uint8_t HUB75_ADDR_MAX = (HUB75_A4 > HUB75_A0123_MAX) ? HUB75_A4 : HUB75_A0123_MAX;

// find num pins within the address pin range
static const uint8_t HUB75_ADDR_PIN_RANGE = HUB75_ADDR_MAX - HUB75_ADDR_BASE + 1;

// Find the minimum of all of the pins
static const uint8_t HUB75_COLOR0_BASE = (HUB75_R0 < HUB75_G0) ? ((HUB75_R0 < HUB75_B0) ? HUB75_R0 : HUB75_B0) : ((HUB75_G0 < HUB75_B0) ? HUB75_G0 : HUB75_B0);
static const uint8_t HUB75_COLOR1_BASE = (HUB75_R1 < HUB75_G1) ? ((HUB75_R1 < HUB75_B1) ? HUB75_R1 : HUB75_B1) : ((HUB75_G1 < HUB75_B1) ? HUB75_G1 : HUB75_B1);
static const uint8_t HUB75_COLOR_BASE = (HUB75_COLOR0_BASE < HUB75_COLOR1_BASE) ? HUB75_COLOR0_BASE : HUB75_COLOR1_BASE;

// Size of framebuffer - all images for animations must be stored here
#define DEF_FRAMEBUFFER_SIZE (128 * 1024)

// analong brightness control pin - set to 0 if we don't have this pin connected
static const uint8_t BRIGHTNESS_PIN = 0;  // Must be and ADC pin
static const uint8_t BRIGHTNESS_ADC = BRIGHTNESS_PIN - 26;

// LED panel resolution within a daisy chain
static const uint32_t LED_PANEL_WIDTH = 128;
static const uint32_t LED_PANEL_HEIGHT = 32;

// The desired refresh of the panel in Hz
static const uint32_t LED_REFRESH_HZ = 100;

// Period of the panel refresh in usec
static const uint32_t LED_REFRESH_PERIOD_USEC = 1000000 / LED_REFRESH_HZ;

// Bits per component
static const uint8_t LED_BPC = 4;

// Maximum component value
static const uint32_t LED_MAX_VALUE = (1 << LED_BPC) - 1;

// number of rows of panels chained together
static const uint32_t LED_PANEL_ROWS = 1;

// the true height of each panel
static const uint32_t LED_PHYSICAL_HEIGHT = (LED_PANEL_HEIGHT / LED_PANEL_ROWS);

// indicates height of the subpanel interleave
static const uint32_t LED_SUBPANEL_HEIGHT = LED_PHYSICAL_HEIGHT / 2;

// number of pio control instructions outside of the main on/off delay loops
// these instructions will keep the LED off, so the maximum brightness will be reduced by this much
static const uint32_t PIO_CTRL_INSTRUCTIONS = 5;

// number of cycles in each control loop for each bit/row
// This affects the maximum brightness granulrity
static const uint32_t PIO_CTRL_CYCLES = 256;

// maximum brightness level
static const uint32_t MAX_BRIGHTNESS = PIO_CTRL_CYCLES - PIO_CTRL_INSTRUCTIONS;

static const uint32_t ADC_MAX_VALUE = 4096;
static const uint32_t ADC_BRIGHTNESS_SCALE = (ADC_MAX_VALUE * ADC_MAX_VALUE) / PIO_CTRL_CYCLES;

// compute pio cycles to complete a frame
static const uint32_t MIN_CYCLES_PER_BIT = PIO_CTRL_CYCLES;
static const uint32_t CYCLES_PER_ROW = (LED_MAX_VALUE + 1) * MIN_CYCLES_PER_BIT;
static const uint32_t CYCLES_PER_FRAME = CYCLES_PER_ROW * LED_SUBPANEL_HEIGHT;

// multiply to get the pio frequency
static const float PIO_CTRL_FREQ = CYCLES_PER_FRAME * LED_REFRESH_HZ;
static const float PIO_DATA_FREQ = 3.0f * (LED_PANEL_WIDTH * PIO_CTRL_FREQ / MIN_CYCLES_PER_BIT);

// RGB masks
static const uint32_t LED_SRC_565_R_MASK_LO = 0xf800;
static const uint32_t LED_SRC_565_G_MASK_LO = 0x07e0;
static const uint32_t LED_SRC_565_B_MASK_LO = 0x001f;
static const uint32_t LED_SRC_565_R_MASK_HI = (LED_SRC_565_R_MASK_LO << 16);
static const uint32_t LED_SRC_565_G_MASK_HI = (LED_SRC_565_G_MASK_LO << 16);
static const uint32_t LED_SRC_565_B_MASK_HI = (LED_SRC_565_B_MASK_LO << 16);
static const uint32_t LED_SRC_565_R_MASK = LED_SRC_565_R_MASK_HI | LED_SRC_565_R_MASK_LO;
static const uint32_t LED_SRC_565_G_MASK = LED_SRC_565_G_MASK_HI | LED_SRC_565_G_MASK_LO;
static const uint32_t LED_SRC_565_B_MASK = LED_SRC_565_B_MASK_HI | LED_SRC_565_B_MASK_LO;

// Smallest quanta of wait time after latching a row in usec
static const uint32_t LED_ROW_WAIT_USEC = 1000000 / (LED_REFRESH_HZ * LED_SUBPANEL_HEIGHT * LED_MAX_VALUE);

static const uint32_t BLACK[4] = { 0 };

#define MAX_ANIM 8

typedef struct ANIM_PARAM_T_ {
    int16_t start_value;
    int16_t delta;
    int16_t num_frames;
    int16_t frame_count;
} ANIM_PARAM_T;

typedef struct ANIM_PARAM_PTR_T_ {
    const char* start_value;
    int32_t delta;
    int16_t frames_until_delta;
    int16_t iterations_until_restart;
    int16_t frame_count;
    int16_t iteration_count;
} ANIM_PARAM_PTR_T;

typedef struct ANIM_T_ {
    int16_t width;
    int16_t pitch;
    int16_t height;
    ANIM_PARAM_PTR_T src_data;
    ANIM_PARAM_T x;
    ANIM_PARAM_T y;
    int16_t num_frames;
} ANIM_T;

typedef struct ANIM_SEQ_T_ {
    ANIM_T anim[MAX_ANIM];
    uint8_t num_anim;
    uint8_t cur_anim;
    int16_t frame_count;
} ANIM_SEQ_T;

typedef struct BACKDROP_T_{
    const uint32_t* src_data;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t pitch;
    uint16_t height;
} BACKDROP_T;

typedef struct IMG_BOX_T_ {
    const char* src_data;
    const FONT_T* font;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int16_t offset_x;
    int16_t offset_y;
    int16_t src_width;
    int16_t src_height;
    uint16_t fg_color;
    uint16_t bg_color;
    const uint8_t* src_sample[2];
    int32_t src_row_index[2];
    int16_t font_row[2];
    int32_t src_col_index;
    int16_t font_col;
} IMG_BOX_T;

enum TCP_CMD_T {
    CMD_NONE,
    CMD_START,
    CMD_BRIGHTNESS,
    CMD_OFFSET,
    CMD_FONT,
    CMD_TEXT_OFFSET,
    CMD_TEXT_SCROLL,
    CMD_OVERLAY_ENABLE,
    CMD_DATA_HEADER,
    CMD_DATA,
    CMD_BACK_ANIM_HEADER,
    CMD_BACK_ANIM,
    CMD_OVER_ANIM_HEADER,
    CMD_OVER_ANIM
};

static const uint8_t ANIM_SIG = 0xDB;

typedef struct ANIM_SAVE_STATE_T_ {
    uint8_t signature;
    uint8_t brightness;
    BACKDROP_T backdrop;
    bool overlay_enable;
    IMG_BOX_T overlay;
    ANIM_SEQ_T back_anim;
    ANIM_SEQ_T over_anim;
} ANIM_SAVE_STATE_T;

typedef struct TCP_SERVER_T_ {
    char hostname[16];
    struct tcp_pcb* server_pcb;
    struct tcp_pcb* client_pcb;
    bool server_ok;
    bool ascii_mode;
    enum TCP_CMD_T cmd;
    uint32_t cur_value;
    uint32_t arg_len;
    uint16_t* img_data;
    ANIM_SAVE_STATE_T anim;
} TCP_SERVER_T;

// flash functions
void flash_save();
void flash_load();
void flash_set_valid(bool valid);

// Server functions
int tcp_server_init(TCP_SERVER_T* state);
void tcp_server_deinit();
bool tcp_server_open(void* arg);
err_t tcp_server_send_data(void* arg, struct tcp_pcb* tpcb, const char* data);
err_t tcp_server_recv(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err);

#endif  // __LED_MAT_H
