// Driver for led matrix for PICO W
// Includes a tcp server to control the display

#include "led-mat.h"

#define TCP_PORT 4242
#define POLL_TIME_S 5

extern uint32_t image_data[DEF_FRAMEBUFFER_SIZE / 4];

int tcp_server_init(TCP_SERVER_T* state)
{
    uint8_t id[FLASH_UNIQUE_ID_SIZE_BYTES];
    if (cyw43_arch_init()) {
        return -1;
    }

    flash_get_unique_id(id);
    int i;
    uint16_t id_sum = 0;
    for (i = 0; i < FLASH_UNIQUE_ID_SIZE_BYTES; i++) {
        id_sum += (i & 1) ? ((uint16_t)id[i]) << 8 : id[i];
    }
    sprintf(state->hostname, "picoled%04x", id_sum);
    cyw43_arch_set_hostname(state->hostname);

    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        return -1;
    }
    return 0;
}

void tcp_server_deinit()
{
    cyw43_arch_deinit();
}

static err_t tcp_server_close(void* arg) {
    TCP_SERVER_T* state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;
    if (state->client_pcb != NULL) {
        tcp_arg(state->client_pcb, NULL);
        tcp_poll(state->client_pcb, NULL, 0);
        tcp_sent(state->client_pcb, NULL);
        tcp_recv(state->client_pcb, NULL);
        tcp_err(state->client_pcb, NULL);
        err = tcp_close(state->client_pcb);
        if (err != ERR_OK) {
            tcp_abort(state->client_pcb);
            err = ERR_ABRT;
        }
        state->client_pcb = NULL;
    }
    //if (state->server_pcb) {
    //    tcp_arg(state->server_pcb, NULL);
    //    tcp_close(state->server_pcb);
    //    state->server_pcb = NULL;
    //}
    return err;
}

static err_t tcp_server_sent(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    return ERR_OK;
}

err_t tcp_server_send_data(void* arg, struct tcp_pcb* tpcb, const char* data)
{
    TCP_SERVER_T* state = (TCP_SERVER_T*)arg;

    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    err_t err = tcp_write(tpcb, data, strlen(data), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return tcp_server_close(arg);
    }
    return ERR_OK;
}

void tcp_server_load_anim(TCP_SERVER_T* state, ANIM_SEQ_T* anim_seq)
{
    ANIM_T* anim = &(anim_seq->anim[anim_seq->cur_anim]);
    switch (state->arg_len) {
    case 28:
        anim->src_data.start_value = (uint8_t*)image_data + state->cur_value;
        state->cur_value = 0;
        break;
    case 26:
        anim->width = state->cur_value;
        state->cur_value = 0;
        break;
    case 24:
        anim->pitch = state->cur_value;
        state->cur_value = 0;
        break;
    case 22:
        anim->height = state->cur_value;
        state->cur_value = 0;
        break;
    case 20:
        anim->x.start_value = state->cur_value;
        state->cur_value = 0;
        break;
    case 18:
        anim->y.start_value = state->cur_value;
        state->cur_value = 0;
        break;
    case 14:
        anim->src_data.delta = state->cur_value;
        state->cur_value = 0;
        break;
    case 12:
        anim->x.delta = state->cur_value;
        state->cur_value = 0;
        break;
    case 10:
        anim->x.num_frames = state->cur_value;
        state->cur_value = 0;
        break;
    case 8:
        anim->y.delta = state->cur_value;
        state->cur_value = 0;
        break;
    case 6:
        anim->y.num_frames = state->cur_value;
        state->cur_value = 0;
        break;
    case 4:
        anim->src_data.iterations_until_restart = state->cur_value;
        state->cur_value = 0;
        break;
    case 2:
        anim->src_data.frames_until_delta = state->cur_value;
        state->cur_value = 0;
        break;
    case 0:
        anim->num_frames = state->cur_value;
        state->cur_value = 0;
        anim->src_data.frame_count = 0;
        anim->src_data.iteration_count = 0;
        anim->x.frame_count = 0;
        anim->y.frame_count = 0;
        anim_seq->cur_anim++;
        if (anim_seq->cur_anim == anim_seq->num_anim) {
            anim_seq->cur_anim = 0;
            anim_seq->frame_count = 0;
            state->cmd = CMD_NONE;
            tcp_server_send_data(state, state->client_pcb, "[OK]\r\n");
            state->arg_len = 0;
        } else {
            state->arg_len = 32;
        }
        break;
    }
}

err_t tcp_server_recv(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    TCP_SERVER_T* state = (TCP_SERVER_T*)arg;
    if (!p) {
        return tcp_server_close(arg);
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    uint16_t i;
    const char* data = (const char*)p->payload;
    for (i = 0; i < p->tot_len; i++) {
        switch (state->cmd) {
        case CMD_NONE:
            if (data[i] == '@') {
                state->cmd = CMD_START;
                state->cur_value = 0;
                state->arg_len = 0;
            }
            break;
        case CMD_START:
            switch (data[i]) {
            case '@':
                state->cmd = CMD_START;
                state->cur_value = 0;
                state->arg_len = 0;
                break;
            case 'A':
                state->cmd = CMD_BACK_ANIM_HEADER;
                state->arg_len = 1;
                break;
            case 'B':
                state->cmd = CMD_BRIGHTNESS;
                state->arg_len = 1;
                break;
            case 'O':
                state->cmd = CMD_OFFSET;
                state->arg_len = 4;
                break;
            case 'V':
                state->cmd = CMD_OVERLAY_ENABLE;
                state->arg_len = 1;
                break;
            case 'F':
                state->cmd = CMD_FONT;
                state->arg_len = 1;
                break;
            case 'T':
                state->cmd = CMD_TEXT_OFFSET;
                state->arg_len = 4;
                break;
            case 'S':
                state->cmd = CMD_TEXT_SCROLL;
                state->arg_len = 4;
                break;
            case 'D':
                state->cmd = CMD_DATA_HEADER;
                state->arg_len = 8;
                break;
            case 'a':
                state->ascii_mode = true;
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            case 'b':
                state->ascii_mode = false;
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            case 'W':
                flash_save();
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            case 'R':
                flash_load();
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            case 'E':
                flash_set_valid(false);
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            case 'U':
                flash_set_valid(true);
                state->cmd = CMD_NONE;
                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                break;
            default:
                state->cmd = CMD_NONE;
                break;
            }
            break;
        default:
            if (state->ascii_mode) {
                switch (data[i]) {
                case '@':
                    state->cmd = CMD_START;
                    state->cur_value = 0;
                    state->arg_len = 0;
                    break;
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
                    state->cur_value *= 10;
                    state->cur_value += data[i] - '0';
                    break;
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                case '\v':
                case '\f':
                    switch (state->cmd) {
                    case CMD_BRIGHTNESS:
                        if (state->arg_len == 1) {
                            state->anim.brightness = (state->cur_value >= MAX_BRIGHTNESS) ? MAX_BRIGHTNESS - 1 : state->cur_value;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_OVERLAY_ENABLE:
                        if (state->arg_len == 1) {
                            state->anim.overlay_enable = (state->cur_value != 0) ? true : false;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_OFFSET:
                        if (state->arg_len == 4) {
                            state->anim.backdrop.y = state->cur_value;
                            state->arg_len -= 2;
                        } else if (state->arg_len == 2) {
                            state->anim.backdrop.x = state->cur_value;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_FONT:
                        if (state->arg_len == 1) {
                            if (state->cur_value >= MAX_FONT) state->cur_value = 0;
                            state->anim.overlay.font = font[state->cur_value];
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_TEXT_OFFSET:
                        if (state->arg_len == 4) {
                            state->anim.overlay.x = state->cur_value;
                            state->arg_len -= 2;
                        } else if (state->arg_len == 2) {
                            state->anim.overlay.y = state->cur_value;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_TEXT_SCROLL:
                        if (state->arg_len == 4) {
                            state->anim.overlay.offset_x = state->cur_value;
                            state->arg_len -= 2;
                        } else if (state->arg_len == 2) {
                            state->anim.overlay.offset_y = state->cur_value;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            state->arg_len = 0;
                        }
                        break;
                    case CMD_DATA_HEADER:
                        if (state->arg_len == 8) {
                            state->img_data = ((uint16_t*)image_data) + state->cur_value;
                            state->arg_len -= 4;
                        } else if (state->arg_len == 4) {
                            state->cmd = CMD_DATA;
                            state->arg_len = state->cur_value;
                        }
                        break;
                    case CMD_DATA:
                        *state->img_data = state->cur_value;
                        state->img_data++;
                        state->arg_len--;
                        if (state->arg_len == 0) {
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                        }
                        break;
                    }
                    state->cur_value = 0;
                    break;
                }
            } else {
                if (state->arg_len > 0) {
                    state->cur_value <<= 8;
                    state->cur_value |= data[i];
                    state->arg_len--;
                    switch (state->cmd) {
                    case CMD_BACK_ANIM_HEADER:
                        state->anim.back_anim.num_anim = (state->cur_value >= MAX_ANIM) ? MAX_ANIM : state->cur_value;
                        state->cmd = CMD_BACK_ANIM;
                        state->anim.back_anim.cur_anim = 0;
                        state->cur_value = 0;
                        state->arg_len = 32;
                        break;
                    case CMD_BACK_ANIM:
                        tcp_server_load_anim(state, &(state->anim.back_anim));
                        break;
                    case CMD_BRIGHTNESS:
                        state->anim.brightness = (state->cur_value >= MAX_BRIGHTNESS) ? MAX_BRIGHTNESS - 1 : state->cur_value;
                        tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                        state->cmd = CMD_NONE;
                        state->arg_len = 0;
                        state->cur_value = 0;
                        break;
                    case CMD_OFFSET:
                        switch (state->arg_len) {
                        case 2:
                            state->anim.backdrop.y = state->cur_value;
                            state->cur_value = 0;
                            break;
                        case 0:
                            state->anim.backdrop.x = state->cur_value;
                            state->cur_value = 0;
                            tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                            state->cmd = CMD_NONE;
                            break;
                        }
                        break;
                    case CMD_OVERLAY_ENABLE:
                        state->anim.overlay_enable = (state->cur_value != 0) ? true : false;
                        tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                        state->cmd = CMD_NONE;
                        state->arg_len = 0;
                        state->cur_value = 0;
                        break;
                    case CMD_DATA_HEADER:
                        switch(state->arg_len) {
                        case 4:
                            state->img_data = ((uint16_t*)image_data) + state->cur_value;
                            state->cur_value = 0;
                            break;
                        case 0:
                            state->cmd = CMD_DATA;
                            state->arg_len = state->cur_value;
                            state->cur_value = 0;
                            break;
                        }
                        break;
                    case CMD_DATA:
                        if ((state->arg_len & 0x1) == 0) {
                            *state->img_data = state->cur_value;
                            state->cur_value = 0;
                            state->img_data++;
                            if (state->arg_len == 0) {
                                tcp_server_send_data(arg, state->client_pcb, "[OK]\r\n");
                                state->cmd = CMD_NONE;
                            }
                        }
                        break;
                    }
                } else {
                    state->cmd = CMD_NONE;
                    state->cur_value = 0;
                }
            }
        }
    }
    if (p->tot_len > 0) {
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;

}

static err_t tcp_server_poll(void* arg, struct tcp_pcb* tpcb) {
    return ERR_OK;
}

static void tcp_server_err(void* arg, err_t err) {
    if (err != ERR_ABRT) {
        tcp_server_close(arg);
    }
}

static err_t tcp_server_accept(void* arg, struct tcp_pcb* client_pcb, err_t err) {
    TCP_SERVER_T* state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        return ERR_VAL;
    }

    // Close any prior connection
    tcp_server_close(arg);

    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK; // tcp_server_send_data(arg, state->client_pcb, "Ready\r\n");
}

bool tcp_server_open(void* arg) {
    TCP_SERVER_T* state = (TCP_SERVER_T*)arg;
    //DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    state->server_ok = true;
    state->img_data = (uint16_t*)image_data;
    state->cmd = CMD_NONE;
    state->ascii_mode = true;
    state->anim.brightness = 0x8;
    state->anim.backdrop.x = 0;
    state->anim.backdrop.y = 0;
    state->cur_value = 0;
    state->arg_len = 0;
    state->server_ok = true;
    state->anim.overlay_enable = true;

    return true;
}

