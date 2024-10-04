// Driver for led matrix for PICO W
// Includes a tcp server to control the display

#include "led-mat.h"
#include "hub75.pio.h"
//#include "mountains_128x64_rgb565.h"

uint32_t image_data[DEF_FRAMEBUFFER_SIZE / 4];

TCP_SERVER_T tcp_state;

const uint32_t flash_header_size = (sizeof(ANIM_SAVE_STATE_T) + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
const uint32_t flash_size = flash_header_size + DEF_FRAMEBUFFER_SIZE;
const uint32_t flash_offset = PICO_FLASH_SIZE_BYTES - flash_size;
const uint32_t flash_image_offset = flash_offset + flash_header_size;

const ANIM_SAVE_STATE_T* anim_save = (const ANIM_SAVE_STATE_T*)(XIP_BASE + flash_offset);
const uint8_t* image_save = (const uint8_t*)(XIP_BASE + flash_image_offset);

//const uint32_t* image_data = (const uint32_t*)mountains_128x64;

void flash_save()
{
    uint8_t* data = (uint8_t*)&(tcp_state.anim);
    uint32_t status = save_and_disable_interrupts();
    flash_range_erase(flash_offset, flash_size);
    flash_range_program(flash_offset, data, flash_header_size);
    data = (uint8_t*)image_data;
    flash_range_program(flash_image_offset, data, DEF_FRAMEBUFFER_SIZE);
    restore_interrupts(status);
}

void flash_load()
{
    if (anim_save->signature == ANIM_SIG) {
        tcp_state.anim = *anim_save;
        memcpy(image_data, image_save, DEF_FRAMEBUFFER_SIZE);
    }
}

void flash_set_valid(bool valid)
{
    ANIM_SAVE_STATE_T temp;
    temp = *anim_save;
    temp.signature = valid ? ANIM_SIG : 0x0;

    uint8_t* data = (uint8_t*)&temp;
    uint32_t status = save_and_disable_interrupts();
    flash_range_erase(flash_offset, flash_header_size);
    flash_range_program(flash_offset, data, flash_header_size);
    restore_interrupts(status);
}

static inline void swap8(uint8_t* a, uint8_t* b)
{
    uint8_t t = *a;
    *a = *b;
    *b = t;
}

static inline uint32_t parse_src565_red(uint32_t src_color0, uint32_t src_color1)
{
    uint32_t comp_color;
    // Get the red components of each pixel, and line them up so that the MSb si at the top of each byte in the DWORD
    comp_color = (src_color0 & LED_SRC_565_R_MASK) >> 8;
    comp_color |= (src_color1 & LED_SRC_565_R_MASK);
    //comp_color = (src_color0 & LED_SRC_565_R_MASK_LO) >> 8;
    //comp_color |= (src_color0 & LED_SRC_565_R_MASK_HI) >> 16;
    //comp_color |= (src_color1 & LED_SRC_565_R_MASK_LO) << 8;
    //comp_color |= (src_color1 & LED_SRC_565_R_MASK_HI);
    return comp_color;
}

static inline uint32_t parse_src565_green(uint32_t src_color0, uint32_t src_color1)
{
    uint32_t comp_color;
    // Get the green components of each pixel, and line them up so that the MSb si at the top of each byte in the DWORD
    comp_color = (src_color0 & LED_SRC_565_G_MASK) >> 3;
    comp_color |= (src_color1 & LED_SRC_565_G_MASK) << 5;
    //comp_color = (src_color0 & LED_SRC_565_G_MASK_LO) >> 3;
    //comp_color |= (src_color0 & LED_SRC_565_G_MASK_HI) >> 11;
    //comp_color |= (src_color1 & LED_SRC_565_G_MASK_LO) << 13;
    //comp_color |= (src_color1 & LED_SRC_565_G_MASK_HI) << 5;
    return comp_color;
}

static inline uint32_t parse_src565_blue(uint32_t src_color0, uint32_t src_color1)
{
    uint32_t comp_color;
    // Get the green components of each pixel, and line them up so that the MSb si at the top of each byte in the DWORD
    comp_color = (src_color0 & LED_SRC_565_B_MASK) << 3;
    comp_color |= (src_color1 & LED_SRC_565_B_MASK) << 11;
    //comp_color = (src_color0 & LED_SRC_565_B_MASK_LO) << 3;
    //comp_color |= (src_color0 & LED_SRC_565_B_MASK_HI) >> 5;
    //comp_color |= (src_color1 & LED_SRC_565_B_MASK_LO) << 19;
    //comp_color |= (src_color1 & LED_SRC_565_B_MASK_HI) << 11;
    return comp_color;
}

void parse_color(uint32_t* dst_data, const uint32_t* src_color)
{
    const uint32_t* pixel;
    uint32_t comp_color;
    uint8_t bit;

    // Top tow
    // parse red component
    comp_color = parse_src565_red(src_color[0], src_color[1]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) = (comp_color & 0x80808080) >> (7 - (HUB75_R0 - HUB75_COLOR_BASE));
        comp_color <<= 1;
    }

    // parse green component
    comp_color = parse_src565_green(src_color[0], src_color[1]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    //// shift that MSb bit down to the desired pin
    //dest_color |= comp_color >> (7 - (HUB75_G0 - HUB75_COLOR_BASE));
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) |= (comp_color & 0x80808080) >> (7 - (HUB75_G0 - HUB75_COLOR_BASE));
        comp_color <<= 1;
    }

    // parse blue component
    comp_color = parse_src565_blue(src_color[0], src_color[1]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    //// shift that MSb bit down to the desired pin
    //dest_color |= comp_color >> (7 - (HUB75_B0 - HUB75_COLOR_BASE));
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) |= (comp_color & 0x80808080) >> (7 - (HUB75_B0 - HUB75_COLOR_BASE));
        comp_color <<= 1;
    }

    // Bottom row
    // parse red component
    comp_color = parse_src565_red(src_color[2], src_color[3]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    //// shift that MSb bit down to the desired pin
    //dest_color |= comp_color >> (7 - (HUB75_R1 - HUB75_COLOR_BASE));
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) |= (comp_color & 0x80808080) >> (7 - (HUB75_R1 - HUB75_COLOR_BASE));
        comp_color <<= 1;
    }

    // parse green component
    comp_color = parse_src565_green(src_color[2], src_color[3]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    //// shift that MSb bit down to the desired pin
    //dest_color |= comp_color >> (7 - (HUB75_G1 - HUB75_COLOR_BASE));
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) |= (comp_color & 0x80808080) >> (7 - (HUB75_G1 - HUB75_COLOR_BASE));
        comp_color <<= 1;
    }

    // parse blue component
    comp_color = parse_src565_blue(src_color[2], src_color[3]);
    // shift up the bit we want to display now to the MSb of each byte, and mask it
    //comp_color <<= LED_BPC - 1 - bit;
    //comp_color &= 0x80808080;
    //// shift that MSb bit down to the desired pin
    //dest_color |= comp_color >> (7 - (HUB75_B1 - HUB75_COLOR_BASE));
    uint8_t* dst_data_addr8;
    for (bit = 0; bit < LED_BPC; bit++) {
        // shift that MSb bit down to the desired pin
        *(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4))) |= (comp_color & 0x80808080) >> (7 - (HUB75_B1 - HUB75_COLOR_BASE));
        comp_color <<= 1;
        dst_data_addr8 = (uint8_t*)(dst_data + ((LED_BPC - 1 - bit) * (LED_PANEL_WIDTH / 4)));
        swap8(dst_data_addr8 + 1, dst_data_addr8 + 2);
    }

    //uint8_t* dest_color_addr = (uint8_t*)&dest_color;
    //swap8(dest_color_addr + 1, dest_color_addr + 2);
}

void read_color(uint32_t* src_color, const uint32_t* src_color_row, uint32_t col)
{
    const uint32_t* pixel;

    // Grab 2 DWORDS, which correspond to 2 pixels
    pixel = src_color_row + (col >> 1);
    src_color[0] = *pixel;
    pixel = (col + 2 >= tcp_state.anim.backdrop.width) ? src_color_row : pixel + 1;
    src_color[1] = *pixel;
    if (col & 0x1) {
        src_color[0] >>= 16;
        src_color[0] |= (src_color[1] << 16);
        src_color[1] >>= 16;
        pixel = (col + 3 == tcp_state.anim.backdrop.width) ? src_color_row : pixel + 1;
        src_color[1] |= (*pixel << 16);
    }
}

void ibox_set_sample(IMG_BOX_T* ibox)
{
    int32_t src_index0 = ibox->src_row_index[0] + ibox->src_col_index;
    int32_t src_index1 = ibox->src_row_index[1] + ibox->src_col_index;

    if (ibox->font) {
        src_index0 = (ibox->src_data[src_index0] * ibox->font->stride) + ibox->font_row[0];
        src_index1 = (ibox->src_data[src_index1] * ibox->font->stride) + ibox->font_row[1];
        ibox->src_sample[0] = ibox->font->glyphs + src_index0;
        ibox->src_sample[1] = ibox->font->glyphs + src_index1;
    } else {
        ibox->src_sample[0] = ibox->src_data + 2 * src_index0;
        ibox->src_sample[1] = ibox->src_data + 2 * src_index1;
    }
}

void ibox_reset_row(IMG_BOX_T* ibox)
{
    int16_t r0 = -ibox->y - ibox->offset_y;
    int16_t r1 = r0 + LED_SUBPANEL_HEIGHT;
    int32_t src_row0 = r0;
    int32_t src_row1 = r1;
    if (ibox->font) {
        src_row0 /= ibox->font->height;
        src_row1 /= ibox->font->height;
        r0 = r0 % ibox->font->height;
        r1 = r1 % ibox->font->height;
        if (r0 < 0) {
            src_row0--;
            r0 += ibox->font->height;
        }
        if (r1 < 0) {
            src_row1--;
            r1 += ibox->font->height;
        }
        ibox->font_row[0] = r0;
        ibox->font_row[1] = r1;
    }
    src_row0 = src_row0 % ibox->src_height;
    src_row1 = src_row1 % ibox->src_height;
    if (src_row0 < 0) src_row0 += ibox->src_height;
    if (src_row1 < 0) src_row1 += ibox->src_height;
    ibox->src_row_index[0] = src_row0 * ibox->src_width;
    ibox->src_row_index[1] = src_row1 * ibox->src_width;
    ibox_set_sample(ibox);
}

void ibox_reset_col(IMG_BOX_T* ibox)
{
    int16_t c = -ibox->x - ibox->offset_x;
    int32_t src_col = c;
    if (ibox->font) {
        src_col /= ibox->font->width;
        c = c % ibox->font->width;
        if (c < 0) {
            src_col--;
            c += ibox->font->width;
        }
        ibox->font_col = c;
    }
    src_col = src_col % ibox->src_width;
    if (src_col < 0) src_col += ibox->src_width;
    ibox->src_col_index = src_col;
    ibox_set_sample(ibox);
}

void ibox_inc_row(IMG_BOX_T* ibox)
{
    bool inc_src_row0 = true;
    bool inc_src_row1 = true;
    int32_t img_size = ibox->src_width * ibox->src_height;
    if (ibox->font) {
        ibox->font_row[0]++;
        ibox->font_row[1]++;
        if (ibox->font_row[0] >= ibox->font->height)
            ibox->font_row[0] -= ibox->font->height;
        else
            inc_src_row0 = false;
        if (ibox->font_row[1] >= ibox->font->height)
            ibox->font_row[1] -= ibox->font->height;
        else
            inc_src_row1 = false;
    }
    if (inc_src_row0) {
        ibox->src_row_index[0] += ibox->src_width;
        if (ibox->src_row_index[0] >= img_size) ibox->src_row_index[0] -= img_size;
    }
    if (inc_src_row1) {
        ibox->src_row_index[1] += ibox->src_width;
        if (ibox->src_row_index[1] >= img_size) ibox->src_row_index[1] -= img_size;
    }
    //ibox_reset_col(ibox);
}

void ibox_inc_col(IMG_BOX_T* ibox)
{
    bool inc_src_col = true;
    if (ibox->font) {
        ibox->font_col++;
        if (ibox->font_col >= ibox->font->width)
            ibox->font_col -= ibox->font->width;
        else
            inc_src_col = false;
    }
    if (inc_src_col) {
        ibox->src_col_index++;
        if (ibox->src_col_index >= ibox->src_width) ibox->src_col_index -= ibox->src_width;
        ibox_set_sample(ibox);
    }
}

void ibox_sample(IMG_BOX_T* ibox, uint32_t* src_color, uint16_t row, uint16_t col)
{
    int16_t i, j;
    int16_t r = row - ibox->y;
    int16_t c = col - ibox->x;
    //int32_t src_index;
    uint16_t char_shift;
    uint32_t img_color;

    // iterate throught the 4 columns
    for (i = 0; i < 4; i++) {
        // Make sure column is in box
        if (c >= 0 && c < ibox->width) {
            r = row - ibox->y;
            // iterate through both rows
            for (j = 0; j < 2; j++) {
                // Make sure row is in box
                if (r >= 0 && r < ibox->height) {
                    bool is_foreground = false;
                    uint16_t foreground_color = ibox->fg_color;
                    //src_index = ibox->src_row_index[j] + ibox->src_col_index;

                    if (ibox->font) {
                        //src_index = (ibox->src_data[src_index] * ibox->font->stride) + ibox->font_row[j];
                        char_shift = 0x7 - ibox->font_col;
                        //is_foreground = (ibox->font->glyphs[src_index] >> char_shift) & 0x1;
                        is_foreground = (*(ibox->src_sample[j]) >> char_shift) & 0x1;
                    } else {
                        //foreground_color = ibox->src_data[2 * src_index];
                        //foreground_color |= ibox->src_data[2 * src_index + 1] << 8;
                        foreground_color = *((uint16_t*)ibox->src_sample[j]);
                        is_foreground = true;
                    }
                    // write out color if it's the foreground or background is not the NO_BACKGROUND color
                    if (is_foreground || ibox->bg_color != NO_BACKGROUND) {
                        uint16_t index = (j << 1) | (i >> 1);
                        img_color = (is_foreground) ? foreground_color : ibox->bg_color;
                        img_color = (i & 0x1) ? img_color << 16 : img_color;
                        src_color[index] &= (i & 0x1) ? 0x0000ffff : 0xffff0000;
                        src_color[index] |= img_color;
                    }
                }
                r += LED_SUBPANEL_HEIGHT;
            }
        }
        ibox_inc_col(ibox);
        c++;
    }
}

void init_row_pins()
{
    // Initilaize the address pins, which the CPU toggles explicitly
    gpio_init(HUB75_A0);
    gpio_init(HUB75_A1);
    gpio_init(HUB75_A2);
    gpio_init(HUB75_A3);
    gpio_init(HUB75_A4);

    gpio_set_dir(HUB75_A0, GPIO_OUT);
    gpio_set_dir(HUB75_A1, GPIO_OUT);
    gpio_set_dir(HUB75_A2, GPIO_OUT);
    gpio_set_dir(HUB75_A3, GPIO_OUT);
    gpio_set_dir(HUB75_A4, GPIO_OUT);

}

uint32_t row_set_mask(uint8_t row)
{
    uint32_t mask = 0;
    uint32_t row32 = row;
    mask |= (row32 & 0x01) << (HUB75_A0 - HUB75_ADDR_BASE - 0);
    mask |= (row32 & 0x02) << (HUB75_A1 - HUB75_ADDR_BASE - 1);
    mask |= (row32 & 0x04) << (HUB75_A2 - HUB75_ADDR_BASE - 2);
    mask |= (row32 & 0x08) << (HUB75_A3 - HUB75_ADDR_BASE - 3);
    mask |= (row32 & 0x10) << (HUB75_A4 - HUB75_ADDR_BASE - 4);
    return mask;
}

//uint32_t row_set_mask(uint8_t row)
//{
//    uint32_t mask = 0;
//    mask |= (row & 0x01) << (HUB75_A0 - 0);
//    mask |= (row & 0x02) << (HUB75_A1 - 1);
//    mask |= (row & 0x04) << (HUB75_A2 - 2);
//    mask |= (row & 0x08) << (HUB75_A3 - 3);
//    mask |= (row & 0x10) << (HUB75_A4 - 4);
//    return mask;
//}

uint32_t row_clr_mask(uint8_t row)
{
    uint32_t mask = 0;
    uint8_t inv_row = ~row;
    mask |= (inv_row & 0x01) << (HUB75_A0 - 0);
    mask |= (inv_row & 0x02) << (HUB75_A1 - 1);
    mask |= (inv_row & 0x04) << (HUB75_A2 - 2);
    mask |= (inv_row & 0x08) << (HUB75_A3 - 3);
    mask |= (inv_row & 0x10) << (HUB75_A4 - 4);
    return mask;
}

void inc_anim_seq(ANIM_SEQ_T* anim_seq, const uint8_t** src_data, int16_t* x, int16_t* y)
{
    ANIM_T* anim = &(anim_seq->anim[anim_seq->cur_anim]);

    // increment animation
    anim_seq->frame_count++;
    anim->src_data.frame_count++;
    anim->x.frame_count++;
    anim->y.frame_count++;

    if (anim->src_data.frame_count >= anim->src_data.frames_until_delta) {
        anim->src_data.frame_count = 0;
        *src_data += anim->src_data.delta;
        anim->src_data.iteration_count++;
        if (anim->src_data.iterations_until_restart != 0 && anim->src_data.iteration_count >= anim->src_data.iterations_until_restart) {
            *src_data = anim->src_data.start_value;// anim->src_data.delta* anim->src_data.iteration_count;
            anim->src_data.iteration_count = 0;
        }
    }

    if (anim->x.frame_count >= anim->x.num_frames) {
        anim->x.frame_count = 0;
        *x += anim->x.delta;
    }

    if (anim->y.frame_count >= anim->y.num_frames) {
        anim->y.frame_count = 0;
        *y += anim->y.delta;
    }

    if (anim_seq->frame_count >= anim->num_frames) {
        anim_seq->frame_count = 0;
        anim_seq->cur_anim++;
        if (anim_seq->cur_anim >= anim_seq->num_anim) {
            anim_seq->cur_anim = 0;
        }
    }
}

void anim_backdrop(TCP_SERVER_T* state)
{
    if (state->anim.back_anim.num_anim == 0 || state->cmd == CMD_BACK_ANIM) return;
    ANIM_T* anim = &(state->anim.back_anim.anim[state->anim.back_anim.cur_anim]);

    // restart animation sequence
    if (state->anim.back_anim.frame_count == 0 && anim->src_data.start_value != NULL) {
        state->anim.backdrop.src_data = (uint32_t*)anim->src_data.start_value;
        if (anim->width) state->anim.backdrop.width = anim->width;
        if (anim->pitch) state->anim.backdrop.pitch = anim->pitch;
        if (anim->height) state->anim.backdrop.height = anim->height;
        state->anim.backdrop.x = anim->x.start_value;
        state->anim.backdrop.y = anim->y.start_value;
        anim->src_data.iteration_count = 0;
        anim->src_data.frame_count = 0;
        anim->x.frame_count = 0;
        anim->y.frame_count = 0;
    }

    // increment animation
    inc_anim_seq(&(state->anim.back_anim), (const uint8_t**)&(state->anim.backdrop.src_data), &(state->anim.backdrop.x), &(state->anim.backdrop.y));
}

void anim_overlay(TCP_SERVER_T* state)
{
    if (state->anim.over_anim.num_anim == 0 || state->cmd == CMD_OVER_ANIM) return;
    ANIM_T* anim = &(state->anim.over_anim.anim[state->anim.over_anim.cur_anim]);

    // restart animation sequence
    if (state->anim.over_anim.frame_count == 0 && anim->src_data.start_value != NULL) {
        state->anim.overlay.src_data = anim->src_data.start_value;
        if (anim->width) state->anim.overlay.src_width = anim->width;
        if (anim->height) state->anim.overlay.src_height = anim->height;
        state->anim.overlay.offset_x = anim->x.start_value;
        state->anim.overlay.offset_y = anim->y.start_value;
        anim->src_data.iteration_count = 0;
        anim->src_data.frame_count = 0;
        anim->x.frame_count = 0;
        anim->y.frame_count = 0;
    }

    // increment animation
    inc_anim_seq(&(state->anim.over_anim), (const uint8_t**)&state->anim.overlay.src_data, &state->anim.overlay.offset_x, &state->anim.overlay.offset_y);
}

//void anim_overlay(TCP_SERVER_T* state)
//{
//    if (state->num_overlay_anim == 0 || state->cmd == CMD_OVER_ANIM) return;
//    ANIM_T* anim = &(state->overlay_anim[state->cur_overlay_anim]);
//    anim->frame_count++;
//    if (anim->frame_count >= anim->num_frames) {
//        anim->frame_count = 0;
//        state->anim.overlay.src_data += anim->delta_src_data;
//        anim->src_inc_count++;
//        if (anim->src_inc_count >= anim->num_src_inc) {
//            state->anim.overlay.src_data -= anim->src_inc_count * anim->delta_src_data;
//            anim->src_inc_count = 0;
//        }
//        state->anim.overlay.offset_x += anim->delta_x;
//        state->anim.overlay.offset_y += anim->delta_y;
//        anim->loop_count++;
//        if (anim->loop_count >= anim->num_loops) {
//            anim->loop_count = 0;
//            state->cur_overlay_anim++;
//            if (state->cur_overlay_anim >= state->num_overlay_anim) {
//                state->cur_overlay_anim = 0;
//            }
//            anim = &(state->overlay_anim[state->cur_overlay_anim]);
//            anim->frame_count = 0;
//            anim->loop_count = 0;
//            if (anim->start_src_data) {
//                state->anim.overlay.src_data = anim->start_src_data;
//                state->anim.overlay.src_width = anim->width;
//                state->anim.overlay.src_height = anim->height;
//                state->anim.overlay.offset_x = anim->start_x;
//                state->anim.overlay.offset_y = anim->start_y;
//                anim->src_inc_count = 0;
//            }
//        }
//    }
//}

int main() {
    stdio_init_all();

    tcp_state.server_ok = !tcp_server_init(&tcp_state);

    tcp_state.anim.signature = ANIM_SIG;
    tcp_state.anim.backdrop.src_data = image_data;
    tcp_state.anim.backdrop.width = LED_PANEL_WIDTH;
    tcp_state.anim.backdrop.pitch = ((tcp_state.anim.backdrop.width + 1) / 2);
    tcp_state.anim.backdrop.height = LED_PANEL_HEIGHT;
    uint32_t r, c, sel, col0, col1;
    for (r = 0; r < tcp_state.anim.backdrop.height; r++) {
        for (c = 0; c < tcp_state.anim.backdrop.pitch; c++) {
            sel = ((c >> 3) & 0x3) | ((r >> 2) & 0x4);
            col0 = ((c & 0x7) << 1) | ((((c & 0x7) << 1) | 0x1) << 16);
            col1 = (r & 0xf) | ((r & 0xf) << 16);
            switch (sel) {
            case 0: col0 = col0 << 12; col1 = col1 << 7;  break;
            case 1: col0 = col0 << 7; col1 = col1 << 1;  break;
            case 2: col0 = col0 << 1; col1 = col1 << 12;  break;
            case 3: col0 = (col0 << 12) | (col0 << 7); col1 = col1 << 1;  break;
            case 4: col0 = (col0 << 7) | (col0 << 1); col1 = col1 << 12;  break;
            case 5: col0 = (col0 << 12) | (col0 << 1); col1 = col1 << 7;  break;
            default: col0 = col0 << 12; col1 = col1 << 7;  break;
            }
            sel = r * tcp_state.anim.backdrop.pitch + c;
            image_data[sel] = col0 | col1;
        }
    }

    if (tcp_state.server_ok) {
        tcp_server_open(&tcp_state);
    } else {
        tcp_state.anim.brightness = 8;
    }

    if (BRIGHTNESS_PIN) {
        adc_init();
        // Make sure GPIO is high-impedance, no pullups etc
        adc_gpio_init(BRIGHTNESS_PIN);
        adc_select_input(BRIGHTNESS_ADC);
    }

    // Choose PIO instance (0 or 1)
    PIO pio = pio0;

    // Get first 2 free state machine in PIO 0
    uint sm_ctrl = pio_claim_unused_sm(pio, true);
    uint sm_data = pio_claim_unused_sm(pio, true);

    // Add PIO program to PIO instruction memory. SDK will find location and
    // return with the memory offset of the program.
    uint ctrl_offset = pio_add_program(pio, &hub75_ctrl_program);
    uint data_offset = pio_add_program(pio, &hub75_data_program);

    // Calculate the PIO clock divider
    // Initialize the program using the helper function in our .pio file
    float div = (float)clock_get_hz(clk_sys) / PIO_DATA_FREQ;
    hub75_data_program_init(pio, sm_data, data_offset, div);
    div = (float)clock_get_hz(clk_sys) / PIO_CTRL_FREQ;
    hub75_ctrl_program_init(pio, sm_ctrl, ctrl_offset, div);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, sm_data, true);
    pio_sm_set_enabled(pio, sm_ctrl, true);
    //init_row_pins();

    uint i, j;
    const uint32_t* pixel0;
    const uint32_t* pixel1;
    uint32_t out_data[LED_BPC * (LED_PANEL_WIDTH / 4)];
    uint32_t dcol;
    uint32_t bit, row;
    int32_t img_row, col;
    uint32_t pio_delays;
    uint32_t set_mask = 0, clr_mask = 0;
    uint32_t src_color[4];
    uint16_t last_brightness = ~0;  // invalid value to ensure the adc pin is read once
    int led = 0;

    tcp_state.anim.back_anim.num_anim = 2;
    tcp_state.anim.back_anim.cur_anim = 0;
    tcp_state.anim.back_anim.frame_count = 0;

    tcp_state.anim.back_anim.anim[0].width = LED_PANEL_WIDTH;
    tcp_state.anim.back_anim.anim[0].pitch = LED_PANEL_WIDTH / 2;
    tcp_state.anim.back_anim.anim[0].height = LED_PANEL_HEIGHT;
    tcp_state.anim.back_anim.anim[0].src_data.start_value = (uint8_t*)image_data;
    tcp_state.anim.back_anim.anim[0].src_data.frames_until_delta = 0;
    tcp_state.anim.back_anim.anim[0].src_data.iterations_until_restart = 0;
    tcp_state.anim.back_anim.anim[0].src_data.delta = 0;
    tcp_state.anim.back_anim.anim[0].src_data.frame_count = 0;
    tcp_state.anim.back_anim.anim[0].src_data.iteration_count = 0;
    tcp_state.anim.back_anim.anim[0].x.start_value = 0;
    tcp_state.anim.back_anim.anim[0].x.delta = -1;
    tcp_state.anim.back_anim.anim[0].x.num_frames = 30;
    tcp_state.anim.back_anim.anim[0].x.frame_count = 0;
    tcp_state.anim.back_anim.anim[0].y.start_value = 0;
    tcp_state.anim.back_anim.anim[0].y.delta = 0;
    tcp_state.anim.back_anim.anim[0].y.num_frames = 0;
    tcp_state.anim.back_anim.anim[0].y.frame_count = 0;
    tcp_state.anim.back_anim.anim[0].num_frames = 480;

    tcp_state.anim.back_anim.anim[1].width = LED_PANEL_WIDTH;
    tcp_state.anim.back_anim.anim[1].pitch = LED_PANEL_WIDTH / 2;
    tcp_state.anim.back_anim.anim[1].height = LED_PANEL_HEIGHT;
    tcp_state.anim.back_anim.anim[1].src_data.start_value = NULL;
    tcp_state.anim.back_anim.anim[1].src_data.frames_until_delta = 0;
    tcp_state.anim.back_anim.anim[1].src_data.iterations_until_restart = 0;
    tcp_state.anim.back_anim.anim[1].src_data.delta = 0;
    tcp_state.anim.back_anim.anim[1].src_data.frame_count = 0;
    tcp_state.anim.back_anim.anim[1].src_data.iteration_count = 0;
    tcp_state.anim.back_anim.anim[1].x.start_value = 0;
    tcp_state.anim.back_anim.anim[1].x.delta = 1;
    tcp_state.anim.back_anim.anim[1].x.num_frames = 30;
    tcp_state.anim.back_anim.anim[1].x.frame_count = 0;
    tcp_state.anim.back_anim.anim[1].y.start_value = 0;
    tcp_state.anim.back_anim.anim[1].y.delta = 0;
    tcp_state.anim.back_anim.anim[1].y.num_frames = 0;
    tcp_state.anim.back_anim.anim[1].y.frame_count = 0;
    tcp_state.anim.back_anim.anim[1].num_frames = 480;

    tcp_state.anim.over_anim.num_anim = 2;
    tcp_state.anim.over_anim.cur_anim = 0;
    tcp_state.anim.over_anim.frame_count = 0;

    tcp_state.anim.over_anim.anim[0].width = (strlen(tcp_state.hostname) + 1) / 2;
    tcp_state.anim.over_anim.anim[0].pitch = tcp_state.anim.over_anim.anim[0].width;
    tcp_state.anim.over_anim.anim[0].height = 2;
    tcp_state.anim.over_anim.anim[0].src_data.start_value = tcp_state.hostname;
    tcp_state.anim.over_anim.anim[0].src_data.frames_until_delta = 0;
    tcp_state.anim.over_anim.anim[0].src_data.iterations_until_restart = 0;
    tcp_state.anim.over_anim.anim[0].src_data.delta = 0;
    tcp_state.anim.over_anim.anim[0].src_data.frame_count = 0;
    tcp_state.anim.over_anim.anim[0].src_data.iteration_count = 0;
    tcp_state.anim.over_anim.anim[0].x.start_value = 0;
    tcp_state.anim.over_anim.anim[0].x.delta = 0;
    tcp_state.anim.over_anim.anim[0].x.num_frames = 0;
    tcp_state.anim.over_anim.anim[0].x.frame_count = 0;
    tcp_state.anim.over_anim.anim[0].y.start_value = 0;
    tcp_state.anim.over_anim.anim[0].y.delta = 1;
    tcp_state.anim.over_anim.anim[0].y.num_frames = 60;
    tcp_state.anim.over_anim.anim[0].y.frame_count = 0;
    tcp_state.anim.over_anim.anim[0].num_frames = 480;

    tcp_state.anim.over_anim.anim[1].width = (strlen(tcp_state.hostname) + 1) / 2;
    tcp_state.anim.over_anim.anim[1].pitch = tcp_state.anim.over_anim.anim[0].width;
    tcp_state.anim.over_anim.anim[1].height = 2;
    tcp_state.anim.over_anim.anim[1].src_data.start_value = NULL;
    tcp_state.anim.over_anim.anim[1].src_data.frames_until_delta = 0;
    tcp_state.anim.over_anim.anim[1].src_data.iterations_until_restart = 0;
    tcp_state.anim.over_anim.anim[1].src_data.delta = 0;
    tcp_state.anim.over_anim.anim[1].src_data.frame_count = 0;
    tcp_state.anim.over_anim.anim[1].src_data.iteration_count = 0;
    tcp_state.anim.over_anim.anim[1].x.start_value = 0;
    tcp_state.anim.over_anim.anim[1].x.delta = 0;
    tcp_state.anim.over_anim.anim[1].x.num_frames = 0;
    tcp_state.anim.over_anim.anim[1].x.frame_count = 0;
    tcp_state.anim.over_anim.anim[1].y.start_value = 0;
    tcp_state.anim.over_anim.anim[1].y.delta = -1;
    tcp_state.anim.over_anim.anim[1].y.num_frames = 60;
    tcp_state.anim.over_anim.anim[1].y.frame_count = 0;
    tcp_state.anim.over_anim.anim[1].num_frames = 480;

    tcp_state.anim.overlay.src_data = tcp_state.hostname;
    tcp_state.anim.overlay.font = &SCRAWL2_F16;
    tcp_state.anim.overlay.offset_x = 0;
    tcp_state.anim.overlay.offset_y = 0;
    tcp_state.anim.overlay.src_width = (strlen(tcp_state.anim.overlay.src_data) + 1) / 2;
    tcp_state.anim.overlay.src_height = 2;
    tcp_state.anim.overlay.x = 64;
    tcp_state.anim.overlay.y = 0;
    tcp_state.anim.overlay.width = tcp_state.anim.overlay.src_width * tcp_state.anim.overlay.font->width;
    tcp_state.anim.overlay.height = tcp_state.anim.overlay.src_height * tcp_state.anim.overlay.font->height;
    tcp_state.anim.overlay.fg_color = 0xf800;
    tcp_state.anim.overlay.bg_color = 0x001f;

    flash_load();

    // Dummy write to start the machine
    pio_sm_put_blocking(pio, sm_ctrl, 0);
    pio_sm_put_blocking(pio, sm_ctrl, 0);

    while (true) {
        anim_backdrop(&tcp_state);
        anim_overlay(&tcp_state);
        ibox_reset_row(&tcp_state.anim.overlay);
        for (row = 0; row < LED_SUBPANEL_HEIGHT; row++) {
            col = (tcp_state.anim.backdrop.x % tcp_state.anim.backdrop.width);
            if (col < 0) col += tcp_state.anim.backdrop.width;
            img_row = (tcp_state.anim.backdrop.y + row) % tcp_state.anim.backdrop.height;
            if (img_row < 0) img_row += tcp_state.anim.backdrop.height;
            pixel0 = tcp_state.anim.backdrop.src_data + (img_row * tcp_state.anim.backdrop.pitch);
            img_row = (img_row + LED_SUBPANEL_HEIGHT) % tcp_state.anim.backdrop.height;
            if (img_row < 0) img_row += tcp_state.anim.backdrop.height;
            pixel1 = tcp_state.anim.backdrop.src_data + (img_row * tcp_state.anim.backdrop.pitch);
            //src_row_index0 = compute_src_row(row, &tcp_state.anim.overlay, &font_row0);
            //src_row_index1 = compute_src_row(row + LED_SUBPANEL_HEIGHT, &tcp_state.anim.overlay, &font_row1);
            ibox_reset_col(&tcp_state.anim.overlay);
            for (i = 0; i < LED_PANEL_WIDTH; i += 4) {
                read_color(src_color, pixel0, col);
                read_color(src_color + 2, pixel1, col);
                if(tcp_state.anim.overlay_enable) ibox_sample(&tcp_state.anim.overlay, src_color, row, i);
                //read_text(src_color, src_row_index0, font_row0, i, &tcp_state.anim.overlay);
                //read_text(src_color + 2, src_row_index1, font_row1, i, &tcp_state.anim.overlay);
                //out_data[i >> 2] = parse_color(src_color, bit);
                parse_color(out_data + (i >> 2), src_color);
                col += 4;
                if (col >= tcp_state.anim.backdrop.width) col -= tcp_state.anim.backdrop.width;
            }
            ibox_inc_row(&tcp_state.anim.overlay);

            // Get the next row setup
            set_mask = row_set_mask(row);
            clr_mask = row_clr_mask(row);
            pio_delays = tcp_state.anim.brightness;
            pio_delays = ((MAX_BRIGHTNESS - pio_delays) << 16) | pio_delays;

            dcol = 0;
            for (bit = 0; bit < LED_BPC; bit++) {
                // wait for acknowledge
                // Without this, we may race ahead and start the next row before the
                // previous one has been latched
                pio_sm_get_blocking(pio, sm_ctrl);

                // send row of data
                for(i=0; i < LED_PANEL_WIDTH / 4; i++) {
                    pio_sm_put_blocking(pio, sm_data, out_data[dcol]);
                    dcol++;
                }

                // Poll the server while before waiting on pio
                if(tcp_state.server_ok) cyw43_arch_poll();

                // Wait for ctrl machine to turn off leds before changing rows
                //pio_sm_get_blocking(pio, sm_ctrl);

                // Send control signals for next row
                //gpio_set_mask(set_mask);
                //gpio_clr_mask(clr_mask);

                // Wait for data sm to finish - make sure data is there before we latch it
                hub75_wait_tx_stall(pio, sm_data);

                pio_sm_put_blocking(pio, sm_ctrl, set_mask);
                pio_sm_put_blocking(pio, sm_ctrl, pio_delays << bit);

            }
        }
        if (BRIGHTNESS_PIN) {
            uint32_t new_brightness = adc_read();
            new_brightness *= new_brightness;
            new_brightness /= ADC_BRIGHTNESS_SCALE;
            if (new_brightness < MAX_BRIGHTNESS && new_brightness != last_brightness) {
                last_brightness = new_brightness;
                tcp_state.anim.brightness = new_brightness;
                if (tcp_state.anim.brightness >= MAX_BRIGHTNESS) tcp_state.anim.brightness = MAX_BRIGHTNESS;
            }
        }
        if (tcp_state.server_ok) {
            // Toggle led every 64 frames
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led & 0x40);
            //if ((led & 0x3f) == 0x0 && tcp_state.client_pcb) {
            //    char buf[32];
            //    static int last_time = 0;
            //    int t = time_us_64() / 1000;
            //    int fps = 64000 / (t - last_time);
            //    sprintf(buf, "%d\r\n", fps);
            //    last_time = t;
            //    tcp_server_send_data(&tcp_state, tcp_state.client_pcb, buf);
            //}
        }
        led++;
    }
}
