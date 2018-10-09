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
#include <inttypes.h>

#include <stdlib.h>
#include "libavutil/adler32.h"
#include "libavutil/aes.h"
#include "libavutil/lfg.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/aes_ctr.h"
//#include "libavutil/audio_fifo.c"

static const DECLARE_ALIGNED(8, uint8_t, plain) [] = { 0x6d, 0x6f, 0x73, 0x74, 0x20, 0x72, 0x61,
        0x6e, 0x64, 0x6f, 0x6d };
static DECLARE_ALIGNED(8, uint8_t, tmp) [11];

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "bbutil.h"

static bool shutdown = false;

#define LEN 7001

static volatile int checksum;

font_t* fontBold16;

static screen_context_t screen_ctx;
static int nScreenWidth, nScreenHeight;

char text[100];

//#define MAX_CHANNELS    32
//
//typedef struct TestStruct
//{
//    const enum AVSampleFormat format;
//    const int nb_ch;
//    void const *data_planes[MAX_CHANNELS];
//    const int nb_samples_pch;
//} TestStruct;
//
//static const uint8_t data_U8[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
//static const int16_t data_S16[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
//static const float data_FLT[] = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0 };
//
//static const TestStruct test_struct[] = { { .format = AV_SAMPLE_FMT_U8, .nb_ch = 1, .data_planes = {
//        data_U8, }, .nb_samples_pch = 12 }, { .format = AV_SAMPLE_FMT_U8P, .nb_ch = 2,
//        .data_planes = { data_U8, data_U8 + 6, }, .nb_samples_pch = 6 }, { .format =
//        AV_SAMPLE_FMT_S16, .nb_ch = 1, .data_planes = { data_S16, }, .nb_samples_pch = 12 }, {
//        .format = AV_SAMPLE_FMT_S16P, .nb_ch = 2, .data_planes = { data_S16, data_S16 + 6, },
//        .nb_samples_pch = 6 }, { .format = AV_SAMPLE_FMT_FLT, .nb_ch = 1, .data_planes =
//        { data_FLT, }, .nb_samples_pch = 12 }, { .format = AV_SAMPLE_FMT_FLTP, .nb_ch = 2,
//        .data_planes = { data_FLT, data_FLT + 6, }, .nb_samples_pch = 6 } };

int initialize()
{
    int dpi = bbutil_calculate_dpi(screen_ctx);

    if (dpi == EXIT_FAILURE) {
        fprintf(stderr, "init(): Unable to calculate dpi\n");
        return EXIT_FAILURE;
    }
    //As bbutil renders text using device-specifc dpi, we need to compute a point size
    //for the font, so that the text string fits into the bubble. Note that Playbook is used
    //as a reference point in this equation as we know that at dpi of 170, font with point size of
    //15 fits into the bubble texture.
    int point_size16 = (int) (16.0f / ((float) dpi / 170.0f));

    fontBold16 = bbutil_load_font("/usr/fonts/font_repository/monotype/arial.ttf", point_size16,
            dpi);
    if (!fontBold16) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

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
    sprintf(text, "%X (expected 50E6E508)", checksum);
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

int test_aes_ctr()
{
    int ret = 1;
    struct AVAESCTR *ae, *ad;
    const uint8_t *iv;

    ae = av_aes_ctr_alloc();
    ad = av_aes_ctr_alloc();

    if (!ae || !ad)
        goto ERROR;

    if (av_aes_ctr_init(ae, (const uint8_t*) "0123456789abcdef") < 0)
        goto ERROR;

    if (av_aes_ctr_init(ad, (const uint8_t*) "0123456789abcdef") < 0)
        goto ERROR;

    av_aes_ctr_set_random_iv(ae);
    iv = av_aes_ctr_get_iv(ae);
    av_aes_ctr_set_full_iv(ad, iv);

    av_aes_ctr_crypt(ae, tmp, plain, sizeof(tmp));
    av_aes_ctr_crypt(ad, tmp, tmp, sizeof(tmp));

    if (memcmp(tmp, plain, sizeof(tmp)) != 0) {
        av_log(NULL, AV_LOG_ERROR, "test failed\n");
        goto ERROR;
    }

    av_log(NULL, AV_LOG_INFO, "test passed\n");
    ret = 0;

    ERROR: av_aes_ctr_free(ae);
    av_aes_ctr_free(ad);
    return ret;
}

//static void free_data_planes(AVAudioFifo *afifo, void **output_data)
//{
//    int i;
//    for (i = 0; i < afifo->nb_buffers; ++i) {
//        av_freep(&output_data[i]);
//    }
//    av_freep(&output_data);
//}
//
//static void ERROR(const char *str)
//{
//    fprintf(stderr, "%s\n", str);
//    exit(1);
//}
//
//static void print_audio_bytes(const TestStruct *test_sample, void **data_planes, int nb_samples)
//{
//    int p, b, f;
//    int byte_offset = av_get_bytes_per_sample(test_sample->format);
//    int buffers = av_sample_fmt_is_planar(test_sample->format) ? test_sample->nb_ch : 1;
//    int line_size =
//            (buffers > 1) ?
//                    nb_samples * byte_offset : nb_samples * byte_offset * test_sample->nb_ch;
//    for (p = 0; p < buffers; ++p) {
//        for (b = 0; b < line_size; b += byte_offset) {
//            for (f = 0; f < byte_offset; f++) {
//                int order = !HAVE_BIGENDIAN ? (byte_offset - f - 1) : f;
//                printf("%02x", *((uint8_t*) data_planes[p] + b + order));
//            }
//            putchar(' ');
//        }
//        putchar('\n');
//    }
//}
//
//static int read_samples_from_audio_fifo(AVAudioFifo* afifo, void ***output, int nb_samples)
//{
//    int i;
//    int samples = FFMIN(nb_samples, afifo->nb_samples);
//    int tot_elements =
//            !av_sample_fmt_is_planar(afifo->sample_fmt) ? samples : afifo->channels * samples;
//    void **data_planes = av_malloc_array(afifo->nb_buffers, sizeof(void*));
//    if (!data_planes)
//        ERROR("failed to allocate memory!");
//    if (*output)
//        free_data_planes(afifo, *output);
//    *output = data_planes;
//
//    for (i = 0; i < afifo->nb_buffers; ++i) {
//        data_planes[i] = av_malloc_array(tot_elements, afifo->sample_size);
//        if (!data_planes[i])
//            ERROR("failed to allocate memory!");
//    }
//
//    return av_audio_fifo_read(afifo, *output, nb_samples);
//}
//
//static int write_samples_to_audio_fifo(AVAudioFifo* afifo, const TestStruct *test_sample,
//        int nb_samples, int offset)
//{
//    int offset_size, i;
//    void *data_planes[MAX_CHANNELS];
//
//    if (nb_samples > test_sample->nb_samples_pch - offset) {
//        return 0;
//    }
//    if (offset >= test_sample->nb_samples_pch) {
//        return 0;
//    }
//    offset_size = offset * afifo->sample_size;
//
//    for (i = 0; i < afifo->nb_buffers; ++i) {
//        data_planes[i] = (uint8_t*) test_sample->data_planes[i] + offset_size;
//    }
//
//    return av_audio_fifo_write(afifo, data_planes, nb_samples);
//}
//
//static void test_function(const TestStruct *test_sample)
//{
//    int ret, i;
//    void **output_data = NULL;
//    AVAudioFifo *afifo = av_audio_fifo_alloc(test_sample->format, test_sample->nb_ch,
//            test_sample->nb_samples_pch);
//    if (!afifo) {
//        ERROR("ERROR: av_audio_fifo_alloc returned NULL!");
//    }
//    ret = write_samples_to_audio_fifo(afifo, test_sample, test_sample->nb_samples_pch, 0);
//    if (ret < 0) {
//        ERROR("ERROR: av_audio_fifo_write failed!");
//    }
//    printf("written: %d\n", ret);
//
//    ret = write_samples_to_audio_fifo(afifo, test_sample, test_sample->nb_samples_pch, 0);
//    if (ret < 0) {
//        ERROR("ERROR: av_audio_fifo_write failed!");
//    }
//    printf("written: %d\n", ret);
//    printf("remaining samples in audio_fifo: %d\n\n", av_audio_fifo_size(afifo));
//
//    ret = read_samples_from_audio_fifo(afifo, &output_data, test_sample->nb_samples_pch);
//    if (ret < 0) {
//        ERROR("ERROR: av_audio_fifo_read failed!");
//    }
//    printf("read: %d\n", ret);
//    print_audio_bytes(test_sample, output_data, ret);
//    printf("remaining samples in audio_fifo: %d\n\n", av_audio_fifo_size(afifo));
//
//    /* test av_audio_fifo_peek */
//    ret = av_audio_fifo_peek(afifo, output_data, afifo->nb_samples);
//    if (ret < 0) {
//        ERROR("ERROR: av_audio_fifo_peek failed!");
//    }
//    printf("peek:\n");
//    print_audio_bytes(test_sample, output_data, ret);
//    printf("\n");
//
//    /* test av_audio_fifo_peek_at */
//    printf("peek_at:\n");
//    for (i = 0; i < afifo->nb_samples; ++i) {
//        ret = av_audio_fifo_peek_at(afifo, output_data, 1, i);
//        if (ret < 0) {
//            ERROR("ERROR: av_audio_fifo_peek_at failed!");
//        }
//        printf("%d:\n", i);
//        print_audio_bytes(test_sample, output_data, ret);
//    }
//    printf("\n");
//
//    /* test av_audio_fifo_drain */
//    ret = av_audio_fifo_drain(afifo, afifo->nb_samples);
//    if (ret < 0) {
//        ERROR("ERROR: av_audio_fifo_drain failed!");
//    }
//    if (afifo->nb_samples) {
//        ERROR("drain failed to flush all samples in audio_fifo!");
//    }
//
//    /* deallocate */
//    free_data_planes(afifo, output_data);
//    av_audio_fifo_free(afifo);
//}

//int test_audio_fifo()
//{
//    int t, tests = sizeof(test_struct) / sizeof(test_struct[0]);
//
//    for (t = 0; t < tests; ++t) {
//        printf("\nTEST: %d\n\n", t + 1);
//        test_function(&test_struct[t]);
//    }
//}

void render()
{
    // Increment the angle by 0.5 degrees
    static float angle = 0.0f;
    angle += 0.5f * M_PI / 180.0f;

    //Typical render pass
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw text
    bbutil_render_text_angle(fontBold16, text, nScreenWidth / 2, nScreenHeight / 2, 0.75f, 0.75f,
            0.75f, 1.0f, angle);

    bbutil_swap();
}

int main(int argc, char **argv)
{
    int exit_application = 0;

    //Create a screen context that will be used to create an EGL surface to to receive libscreen events
    screen_create_context(&screen_ctx, 0);
    // Get display configuration (dimensions)
    int count = 0;
    screen_get_context_property_iv(screen_ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count);
    screen_display_t *screen_disps = (screen_display_t *) calloc(count, sizeof(screen_display_t));
    screen_get_context_property_pv(screen_ctx, SCREEN_PROPERTY_DISPLAYS, (void **) screen_disps);
    screen_display_t screen_disp = screen_disps[0];
    free(screen_disps);
    int dims[2] = { 0, 0 };
    screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_SIZE, dims);
    nScreenWidth = dims[0];
    nScreenHeight = dims[1];

    //Initialize BPS library
    bps_initialize();

    //Use utility code to initialize EGL for rendering with GL ES 2.0
    if (EXIT_SUCCESS != bbutil_init_egl(screen_ctx)) {
        fprintf(stderr, "bbutil_init_egl failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_ctx);
        return 0;
    }

    //Initialize application logic
    if (EXIT_SUCCESS != initialize()) {
        fprintf(stderr, "initialize failed\n");
        bbutil_terminate();
        screen_destroy_context(screen_ctx);
        bps_shutdown();
        return 0;
    }

    test_adler32(argc, argv);
    test_aes_ctr();

    while (!exit_application) {
        //Request and process all available BPS events
        bps_event_t *event = NULL;

        for (;;) {
            if (BPS_SUCCESS != bps_get_event(&event, 0)) {
                fprintf(stderr, "bps_get_event failed\n");
                break;
            }

            if (event) {
                int domain = bps_event_get_domain(event);

                if ((domain == navigator_get_domain())
                        && (NAVIGATOR_EXIT == bps_event_get_code(event))) {
                    exit_application = 1;
                }
            } else {
                break;
            }
        }
        render();
    }

    //Stop requesting events from libscreen
    screen_stop_events(screen_ctx);

    //Shut down BPS library for this process
    bps_shutdown();

    //Use utility code to terminate EGL setup
    bbutil_terminate();

    //Destroy libscreen context
    screen_destroy_context(screen_ctx);
    return 0;
}

