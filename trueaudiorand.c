#include "trueaudiorand.h"

#include "sha256.h"

#include <portaudio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#define TAR_SAMPLE_RATE     44100
#define TAR_CHANNELS        1
#define TAR_FRAMES_PER_READ 1024
#define TAR_VN_BUFFER_BYTES 512
#define TAR_POOL_BYTES      32

typedef struct {
    PaStream *stream;
    int       stream_active;
    int       pa_active;
    uint8_t   pool[TAR_POOL_BYTES];
    int       pool_remaining;
    uint64_t  generation;
    char      device_name[128];
    char      error[256];
} TarState;

static TarState g_tar = {0};

const char *tar_strerror(void)
{
    return g_tar.error[0] ? g_tar.error : "nessun errore";
}

static void tar_set_error(const char *msg)
{
    snprintf(g_tar.error, sizeof(g_tar.error), "%s", msg ? msg : "errore sconosciuto");
}

static void tar_set_pa_error(const char *prefix, PaError err)
{
    const char *pa_err = Pa_GetErrorText(err);
    snprintf(g_tar.error, sizeof(g_tar.error), "%s: %s", prefix, pa_err);
}

/* Su Windows PortAudio espone spesso nomi UTF-16 LE dentro un char*. */
static int tar_is_utf16le(const char *src)
{
    if (!src || !src[0]) {
        return 0;
    }

    /* Segnale forte: 'M', '\0', 'i', ... */
    if (src[1] == '\0' && src[2] != '\0') {
        return 1;
    }

    /* Piu coppie ASCII + null consecutive (es. "Microphone" wide) */
    int pairs = 0;
    for (int i = 0; i < 8; ++i) {
        unsigned char hi = (unsigned char)src[i * 2];
        unsigned char lo = (unsigned char)src[i * 2 + 1];
        if (hi == '\0') {
            break;
        }
        if (lo == '\0' && hi < 0x80) {
            pairs++;
        } else {
            break;
        }
    }

    return pairs >= 3;
}

static void tar_copy_pa_string(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }

#ifdef _WIN32
    {
        size_t narrow_len = strlen(src);
        size_t wide_len   = wcslen((const wchar_t *)src);

        if (tar_is_utf16le(src) || (narrow_len <= 2 && wide_len > narrow_len && wide_len > 1)) {
            const wchar_t *wide = (const wchar_t *)src;
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, dst, (int)dst_size, NULL, NULL);
            if (dst[0] != '\0') {
                return;
            }
        }
    }

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0) > 0) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    {
        wchar_t wbuf[256];
        char utf8[TAR_DEVICE_NAME_MAX];
        MultiByteToWideChar(CP_ACP, 0, src, -1, wbuf, 256);
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, (int)sizeof(utf8), NULL, NULL);
        snprintf(dst, dst_size, "%s", utf8);
    }
#else
    snprintf(dst, dst_size, "%s", src);
#endif
}

static int tar_pa_startup(void)
{
    if (g_tar.pa_active) {
        return 0;
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        tar_set_pa_error("Pa_Initialize", err);
        return -1;
    }

    g_tar.pa_active = 1;
    return 0;
}

static void tar_pa_shutdown_if_idle(void)
{
    if (g_tar.pa_active && !g_tar.stream_active) {
        Pa_Terminate();
        g_tar.pa_active = 0;
    }
}

static int tar_is_input_device(PaDeviceIndex index)
{
    const PaDeviceInfo *info = Pa_GetDeviceInfo(index);
    return info && info->maxInputChannels > 0;
}

static PaDeviceIndex tar_list_index_to_pa_index(int list_index)
{
    if (list_index < 0) {
        return paInvalidDevice;
    }

    int current = 0;
    int total = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < total; ++i) {
        if (!tar_is_input_device(i)) {
            continue;
        }
        if (current == list_index) {
            return i;
        }
        current++;
    }

    return paInvalidDevice;
}

int tar_input_device_count(void)
{
    if (tar_pa_startup() != 0) {
        return -1;
    }

    int count = 0;
    int total = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < total; ++i) {
        if (tar_is_input_device(i)) {
            count++;
        }
    }

    tar_pa_shutdown_if_idle();
    return count;
}

int tar_get_input_device(int list_index, TarInputDevice *out)
{
    if (!out || list_index < 0) {
        tar_set_error("parametri non validi");
        return -1;
    }

    if (tar_pa_startup() != 0) {
        return -2;
    }

    PaDeviceIndex pa_index = tar_list_index_to_pa_index(list_index);
    if (pa_index == paInvalidDevice) {
        tar_set_error("indice dispositivo non valido");
        tar_pa_shutdown_if_idle();
        return -3;
    }

    const PaDeviceInfo *info = Pa_GetDeviceInfo(pa_index);
    const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);

    memset(out, 0, sizeof(*out));
    out->pa_index = pa_index;
    tar_copy_pa_string(info->name, out->name, sizeof(out->name));
    tar_copy_pa_string(api ? api->name : "Unknown", out->host_api_name, sizeof(out->host_api_name));
    out->max_input_channels = info->maxInputChannels;
    out->is_default = (pa_index == Pa_GetDefaultInputDevice());

    tar_pa_shutdown_if_idle();
    return 0;
}

#ifdef _WIN32
int tar_get_input_device_label_w(int list_index, wchar_t *out, size_t out_chars, int *pa_index_out)
{
    TarInputDevice dev;
    wchar_t name[TAR_DEVICE_NAME_MAX];
    wchar_t api[64];

    if (!out || out_chars == 0) {
        tar_set_error("parametri non validi");
        return -1;
    }

    if (tar_get_input_device(list_index, &dev) != 0) {
        return -2;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, dev.name, -1, name, TAR_DEVICE_NAME_MAX) <= 0) {
        MultiByteToWideChar(CP_ACP, 0, dev.name, -1, name, TAR_DEVICE_NAME_MAX);
    }
    if (MultiByteToWideChar(CP_UTF8, 0, dev.host_api_name, -1, api, 64) <= 0) {
        MultiByteToWideChar(CP_ACP, 0, dev.host_api_name, -1, api, 64);
    }

    _snwprintf(out, out_chars, L"[%d] %s  (%s)%s",
               dev.pa_index,
               name,
               api,
               dev.is_default ? L"  *predefinito*" : L"");

    if (pa_index_out) {
        *pa_index_out = dev.pa_index;
    }

    return 0;
}
#endif

static size_t tar_von_neumann_extract(const uint8_t *bits, size_t bit_count, uint8_t *out, size_t out_max)
{
    size_t out_bits = 0;

    for (size_t i = 0; i + 1 < bit_count && (out_bits / 8) < out_max; ++i) {
        uint8_t b0 = (bits[i / 8] >> (7 - (i % 8))) & 1u;
        uint8_t b1 = (bits[(i + 1) / 8] >> (7 - ((i + 1) % 8))) & 1u;

        if (b0 == 0 && b1 == 1) {
            size_t byte_idx = out_bits / 8;
            out[byte_idx] = (uint8_t)(out[byte_idx] << 1);
            out_bits++;
        } else if (b0 == 1 && b1 == 0) {
            size_t byte_idx = out_bits / 8;
            out[byte_idx] = (uint8_t)((out[byte_idx] << 1) | 1u);
            out_bits++;
        }
        ++i;
    }

    return out_bits / 8;
}

static size_t tar_extract_raw_bits(const int16_t *samples, size_t count, uint8_t *out, size_t out_bytes)
{
    size_t bit_idx = 0;
    size_t max_bits = out_bytes * 8;

    for (size_t i = 1; i < count && bit_idx < max_bits; ++i) {
        int32_t delta = (int32_t)samples[i] - (int32_t)samples[i - 1];
        uint8_t bit = (uint8_t)(delta & 1);

        size_t byte_idx = bit_idx / 8;
        size_t shift = 7 - (bit_idx % 8);
        out[byte_idx] = (uint8_t)(out[byte_idx] | (bit << shift));
        bit_idx++;
    }

    return (bit_idx + 7) / 8;
}

static uint32_t tar_bytes_to_u32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

static void tar_copy_preview(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len)
{
    size_t n = src_len < dst_len ? src_len : dst_len;
    memcpy(dst, src, n);
}

static void tar_refill_pool(const int16_t *samples, size_t count, TarPipelineReport *report)
{
    uint8_t raw_bits[TAR_VN_BUFFER_BYTES * 2];
    uint8_t debiased[TAR_VN_BUFFER_BYTES];
    uint8_t hash[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;

    memset(raw_bits, 0, sizeof(raw_bits));
    memset(debiased, 0, sizeof(debiased));

    size_t raw_bytes = tar_extract_raw_bits(samples, count, raw_bits, sizeof(raw_bits));
    size_t debiased_bytes = tar_von_neumann_extract(raw_bits, raw_bytes * 8, debiased, sizeof(debiased));

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)samples, count * sizeof(int16_t));
    sha256_update(&ctx, debiased, debiased_bytes);

    uint64_t gen = ++g_tar.generation;
    sha256_update(&ctx, (const uint8_t *)&gen, sizeof(gen));
    sha256_final(&ctx, hash);

    memcpy(g_tar.pool, hash, TAR_POOL_BYTES);
    g_tar.pool_remaining = TAR_POOL_BYTES;

    if (report) {
        report->frames_captured = count;
        for (int i = 0; i < TAR_DEMO_PREVIEW_SAMPLES && (size_t)i < count; ++i) {
            report->raw_samples[i] = samples[i];
        }
        report->raw_bit_bytes = raw_bytes;
        tar_copy_preview(raw_bits, raw_bytes, report->raw_bits_preview, TAR_DEMO_PREVIEW_BYTES);
        report->debiased_bytes = debiased_bytes;
        tar_copy_preview(debiased, debiased_bytes, report->debiased_preview, TAR_DEMO_PREVIEW_BYTES);
        memcpy(report->sha256_hash, hash, sizeof(report->sha256_hash));
        report->rand32 = tar_bytes_to_u32(hash);
    }
}

static void tar_close_stream(void)
{
    if (g_tar.stream) {
        Pa_StopStream(g_tar.stream);
        Pa_CloseStream(g_tar.stream);
        g_tar.stream = NULL;
    }

    g_tar.stream_active = 0;
    g_tar.pool_remaining = 0;
    g_tar.generation = 0;
}

int tar_init_device(int pa_device_index)
{
    tar_close_stream();

    if (tar_pa_startup() != 0) {
        return -1;
    }

    PaDeviceIndex device;
    if (pa_device_index < 0) {
        device = Pa_GetDefaultInputDevice();
    } else {
        device = (PaDeviceIndex)pa_device_index;
    }

    if (device == paNoDevice) {
        tar_set_error("nessun dispositivo di input audio trovato");
        tar_pa_shutdown_if_idle();
        return -2;
    }

    if (!tar_is_input_device(device)) {
        tar_set_error("il dispositivo selezionato non supporta input audio");
        tar_pa_shutdown_if_idle();
        return -3;
    }

    const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
    if (!info) {
        tar_set_error("impossibile leggere info dispositivo audio");
        tar_pa_shutdown_if_idle();
        return -4;
    }

    PaStreamParameters params;
    params.device                    = device;
    params.channelCount              = TAR_CHANNELS;
    params.sampleFormat              = paInt16;
    params.suggestedLatency          = info->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = NULL;

    double sample_rate = info->defaultSampleRate;
    if (sample_rate <= 0.0) {
        sample_rate = TAR_SAMPLE_RATE;
    }

    PaError err = Pa_OpenStream(
        &g_tar.stream,
        &params,
        NULL,
        sample_rate,
        paFramesPerBufferUnspecified,
        paClipOff,
        NULL,
        NULL);

    if (err != paNoError) {
        tar_set_pa_error("Pa_OpenStream", err);
        tar_pa_shutdown_if_idle();
        return -5;
    }

    err = Pa_StartStream(g_tar.stream);
    if (err != paNoError) {
        tar_set_pa_error("Pa_StartStream", err);
        Pa_CloseStream(g_tar.stream);
        g_tar.stream = NULL;
        tar_pa_shutdown_if_idle();
        return -6;
    }

    g_tar.stream_active = 1;
    g_tar.error[0] = '\0';
    tar_copy_pa_string(info->name, g_tar.device_name, sizeof(g_tar.device_name));
    return 0;
}

int tar_init(void)
{
    return tar_init_device(-1);
}

uint32_t tar_get_rand32(void)
{
    int16_t samples[TAR_FRAMES_PER_READ];

    if (!g_tar.stream_active || !g_tar.stream) {
        tar_set_error("tar_init() non chiamato o stream non attivo");
        return 0;
    }

    if (g_tar.pool_remaining >= 4) {
        int offset = TAR_POOL_BYTES - g_tar.pool_remaining;
        uint32_t value = tar_bytes_to_u32(&g_tar.pool[offset]);
        g_tar.pool_remaining -= 4;
        return value;
    }

    PaError err = Pa_ReadStream(g_tar.stream, samples, TAR_FRAMES_PER_READ);
    if (err != paNoError && err != paInputOverflowed) {
        tar_set_pa_error("Pa_ReadStream", err);
        return 0;
    }

    tar_refill_pool(samples, TAR_FRAMES_PER_READ, NULL);

    uint32_t value = tar_bytes_to_u32(g_tar.pool);
    g_tar.pool_remaining -= 4;
    return value;
}

int tar_demo_once(TarPipelineReport *report)
{
    int16_t samples[TAR_FRAMES_PER_READ];

    if (!report) {
        tar_set_error("report nullo");
        return -1;
    }

    if (!g_tar.stream_active || !g_tar.stream) {
        tar_set_error("tar_init() non chiamato o stream non attivo");
        return -2;
    }

    memset(report, 0, sizeof(*report));
    snprintf(report->device_name, sizeof(report->device_name), "%s", g_tar.device_name);
    report->sample_rate = TAR_SAMPLE_RATE;
    report->channels    = TAR_CHANNELS;

    PaError err = Pa_ReadStream(g_tar.stream, samples, TAR_FRAMES_PER_READ);
    if (err != paNoError && err != paInputOverflowed) {
        tar_set_pa_error("Pa_ReadStream", err);
        return -3;
    }

    tar_refill_pool(samples, TAR_FRAMES_PER_READ, report);
    g_tar.pool_remaining -= 4;
    return 0;
}

void tar_close(void)
{
    tar_close_stream();

    if (g_tar.pa_active) {
        Pa_Terminate();
        g_tar.pa_active = 0;
    }

    g_tar.error[0] = '\0';
}
