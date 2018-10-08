/*
 * Copyright (c) 2011-2015 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <bps/bps.h>
#include <bps/event.h>
#include <bps/navigator.h>
#include <bps/screen.h>
#include <fcntl.h>
#include <screen/screen.h>

#include <stdlib.h>
#include "libavutil/adler32.h"
#include "libavutil/aes.h"
#include "libavutil/lfg.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"

static bool shutdown = false;

#define LEN 7001

static volatile int checksum;

/**
 * Use the PID to set the window group id.
 */
static const char *
get_window_group_id()
{
    static char s_window_group_id[16] = "";

    if (s_window_group_id[0] == '\0') {
        snprintf(s_window_group_id, sizeof(s_window_group_id), "%d", getpid());
    }

    return s_window_group_id;
}

static void handle_screen_event(bps_event_t *event)
{
    int screen_val;

    screen_event_t screen_event = screen_event_get_event(event);
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &screen_val);

    switch (screen_val) {
        case SCREEN_EVENT_MTOUCH_TOUCH:
            fprintf(stderr, "Touch event");
            break;
        case SCREEN_EVENT_MTOUCH_MOVE:
            fprintf(stderr, "Move event");
            break;
        case SCREEN_EVENT_MTOUCH_RELEASE:
            fprintf(stderr, "Release event");
            break;
        default:
            break;
    }
    fprintf(stderr, "\n");
}

static void handle_navigator_event(bps_event_t *event)
{
    switch (bps_event_get_code(event)) {
        case NAVIGATOR_SWIPE_DOWN:
            fprintf(stderr, "Swipe down event");
            break;
        case NAVIGATOR_EXIT:
            fprintf(stderr, "Exit event");
            shutdown = true;
            break;
        default:
            break;
    }
    fprintf(stderr, "\n");
}

static void handle_event()
{
    int domain;

    bps_event_t *event = NULL;
    if (BPS_SUCCESS != bps_get_event(&event, -1)) {
        fprintf(stderr, "bps_get_event() failed\n");
        return;
    }
    if (event) {
        domain = bps_event_get_domain(event);
        if (domain == navigator_get_domain()) {
            handle_navigator_event(event);
        } else if (domain == screen_get_domain()) {
            handle_screen_event(event);
        }
    }
}

int test_adler32(int argc, char **argv)
{
    int i;
    uint8_t data[LEN];

    av_log_set_level(AV_LOG_DEBUG);

    for (i = 0; i < LEN; i++)
        data[i] = ((i * i) >> 3) + 123 * i;

    if (argc > 1 && !strcmp(argv[1], "-t")) {
        for (i = 0; i < 1000; i++) {
            START_TIMER;
            checksum = av_adler32_update(1, data, LEN);
            STOP_TIMER("adler");
        }
    } else {
        checksum = av_adler32_update(1, data, LEN);
    }

    av_log(NULL, AV_LOG_DEBUG, "%X (expected 50E6E508)\n", checksum);
    return checksum == 0x50e6e508 ? 0 : 1;
}

int test_aes(int argc, char **argv)
{
    int i, j;
    struct AVAES *b;
    static const uint8_t rkey[2][16] = { { 0 }, { 0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3,
            0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59 } };
    static const uint8_t rpt[2][16] = { { 0x6a, 0x84, 0x86, 0x7c, 0xd7, 0x7e, 0x12, 0xad, 0x07,
            0xea, 0x1b, 0xe8, 0x95, 0xc5, 0x3f, 0xa3 }, { 0 } };
    static const uint8_t rct[2][16] = { { 0x73, 0x22, 0x81, 0xc0, 0xa0, 0xaa, 0xb8, 0xf7, 0xa5,
            0x4a, 0x0c, 0x67, 0xa0, 0xc4, 0x5e, 0xcf }, { 0x6d, 0x25, 0x1e, 0x69, 0x44, 0xb0, 0x51,
            0xe0, 0x4e, 0xaa, 0x6f, 0xb4, 0xdb, 0xf7, 0x84, 0x65 } };
    uint8_t pt[32];
    uint8_t temp[32];
    uint8_t iv[2][16];
    int err = 0;

    b = av_aes_alloc();
    if (!b)
        return 1;

    av_log_set_level(AV_LOG_DEBUG);

    for (i = 0; i < 2; i++) {
        av_aes_init(b, rkey[i], 128, 1);
        av_aes_crypt(b, temp, rct[i], 1, NULL, 1);
        for (j = 0; j < 16; j++) {
            if (rpt[i][j] != temp[j]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02X %02X\n", j, rpt[i][j], temp[j]);
                err = 1;
            }
        }
    }
    av_free(b);

    if (argc > 1 && !strcmp(argv[1], "-t")) {
        struct AVAES *ae, *ad;
        AVLFG prng;

        ae = av_aes_alloc();
        ad = av_aes_alloc();

        if (!ae || !ad) {
            av_free(ae);
            av_free(ad);
            return 1;
        }

        av_aes_init(ae, (const uint8_t*) "PI=3.141592654..", 128, 0);
        av_aes_init(ad, (const uint8_t*) "PI=3.141592654..", 128, 1);
        av_lfg_init(&prng, 1);

        for (i = 0; i < 10000; i++) {
            for (j = 0; j < 32; j++)
                pt[j] = av_lfg_get(&prng);
            for (j = 0; j < 16; j++)
                iv[0][j] = iv[1][j] = av_lfg_get(&prng);
            {
                START_TIMER;
                av_aes_crypt(ae, temp, pt, 2, iv[0], 0);
                if (!(i & (i - 1)))
                    av_log(NULL, AV_LOG_ERROR, "%02X %02X %02X %02X\n", temp[0], temp[5], temp[10],
                            temp[15]);
                av_aes_crypt(ad, temp, temp, 2, iv[1], 1);
                av_aes_crypt(ae, temp, pt, 2, NULL, 0);
                if (!(i & (i - 1)))
                    av_log(NULL, AV_LOG_ERROR, "%02X %02X %02X %02X\n", temp[0], temp[5], temp[10],
                            temp[15]);
                av_aes_crypt(ad, temp, temp, 2, NULL, 1);
                STOP_TIMER("aes");
            }
            for (j = 0; j < 16; j++) {
                if (pt[j] != temp[j]) {
                    av_log(NULL, AV_LOG_ERROR, "%d %d %02X %02X\n", i, j, pt[j], temp[j]);
                }
            }
        }
        av_free(ae);
        av_free(ad);
    }
    return err;
}

int main(int argc, char **argv)
{
    const int usage = SCREEN_USAGE_NATIVE;

    screen_context_t screen_ctx;
    screen_window_t screen_win;
    screen_buffer_t screen_buf = NULL;
    int rect[4] = { 0, 0, 0, 0 };

    /* Setup the window */
    screen_create_context(&screen_ctx, 0);
    screen_create_window(&screen_win, screen_ctx);
    screen_create_window_group(screen_win, get_window_group_id());
    screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_USAGE, &usage);
    screen_create_window_buffers(screen_win, 1);

    screen_get_window_property_pv(screen_win, SCREEN_PROPERTY_RENDER_BUFFERS,
            (void **) &screen_buf);
    screen_get_window_property_iv(screen_win, SCREEN_PROPERTY_BUFFER_SIZE, rect + 2);

    /* Fill the screen buffer with blue */
    int attribs[] = { SCREEN_BLIT_COLOR, 0xff0000ff, SCREEN_BLIT_END };
    screen_fill(screen_ctx, screen_buf, attribs);
    screen_post_window(screen_win, screen_buf, 1, rect, 0);

    /* Signal bps library that navigator and screen events will be requested */
    bps_initialize();
    screen_request_events(screen_ctx);
    navigator_request_events(0);

    test_adler32(argc, argv);
    test_aes(argc, argv);

    while (!shutdown) {
        /* Handle user input */
        handle_event();
    }

    /* Clean up */
    screen_stop_events(screen_ctx);
    bps_shutdown();
    screen_destroy_window(screen_win);
    screen_destroy_context(screen_ctx);
    return 0;
}

