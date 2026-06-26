#ifndef TRUEAUDIORAND_H
#define TRUEAUDIORAND_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <wchar.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TAR_DEMO_PREVIEW_SAMPLES 8
#define TAR_DEMO_PREVIEW_BYTES   8
#define TAR_DEVICE_NAME_MAX      256

typedef struct {
    int  pa_index;
    char name[TAR_DEVICE_NAME_MAX];
    char host_api_name[64];
    int  max_input_channels;
    int  is_default;
} TarInputDevice;

typedef struct {
    char     device_name[128];
    int      sample_rate;
    int      channels;
    size_t   frames_captured;
    int16_t  raw_samples[TAR_DEMO_PREVIEW_SAMPLES];
    size_t   raw_bit_bytes;
    uint8_t  raw_bits_preview[TAR_DEMO_PREVIEW_BYTES];
    size_t   debiased_bytes;
    uint8_t  debiased_preview[TAR_DEMO_PREVIEW_BYTES];
    uint8_t  sha256_hash[32];
    uint32_t rand32;
} TarPipelineReport;

/* Numero di dispositivi di input disponibili (>= 0) o codice negativo. */
int tar_input_device_count(void);

/* Riempie out con il dispositivo all'indice di lista [0, count). */
int tar_get_input_device(int list_index, TarInputDevice *out);

#ifdef _WIN32
/* Etichetta completa per GUI Windows (UTF-16 nativo PortAudio). */
int tar_get_input_device_label_w(int list_index, wchar_t *out, size_t out_chars, int *pa_index_out);
#endif

/* Inizializza con il microfono predefinito di sistema. */
int tar_init(void);

/* Inizializza con un dispositivo PortAudio specifico (pa_index).
 * Passa -1 per il dispositivo predefinito. */
int tar_init_device(int pa_device_index);

uint32_t tar_get_rand32(void);
int tar_demo_once(TarPipelineReport *report);
void tar_close(void);
const char *tar_strerror(void);

#ifdef __cplusplus
}
#endif

#endif /* TRUEAUDIORAND_H */
