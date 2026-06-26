#include "trueaudiorand.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        printf("%02x", data[i]);
        if (i + 1 < len) {
            printf(" ");
        }
    }
}

static void print_pipeline_report(const TarPipelineReport *r)
{
    printf("\n");
    printf("============================================================\n");
    printf("  PIPELINE TrueAudioRand - ciclo completo\n");
    printf("============================================================\n\n");

    printf("[1] MICROFONO\n");
    printf("    Dispositivo : %s\n", r->device_name);
    printf("    Formato     : %d Hz, %d canale/i, PCM 16-bit\n\n",
           r->sample_rate, r->channels);

    printf("[2] CATTURA AUDIO (%zu campioni PCM)\n", r->frames_captured);
    printf("    Anteprima primi %d valori grezzi:\n    ",
           TAR_DEMO_PREVIEW_SAMPLES);
    for (int i = 0; i < TAR_DEMO_PREVIEW_SAMPLES; ++i) {
        printf("%6d", (int)r->raw_samples[i]);
        if (i + 1 < TAR_DEMO_PREVIEW_SAMPLES) {
            printf(", ");
        }
    }
    printf("\n\n");

    printf("[3] ESTRAZIONE BIT (delta campioni consecutivi, LSB)\n");
    printf("    Bit grezzi raccolti : %zu byte\n", r->raw_bit_bytes);
    printf("    Anteprima           : ");
    print_hex(r->raw_bits_preview, TAR_DEMO_PREVIEW_BYTES);
    printf("\n\n");

    printf("[4] DEBIASING Von Neumann (01->0, 10->1, scarta 00/11)\n");
    printf("    Byte sbiancati      : %zu\n", r->debiased_bytes);
    printf("    Anteprima           : ");
    print_hex(r->debiased_preview, TAR_DEMO_PREVIEW_BYTES);
    printf("\n\n");

    printf("[5] HASH SHA-256 (estrattore di casualita)\n");
    printf("    Input  : buffer PCM + bit debiased + contatore\n");
    printf("    Output : ");
    print_hex(r->sha256_hash, 32);
    printf("\n\n");

    printf("[6] RISULTATO FINALE\n");
    printf("    uint32_t = %" PRIu32 "  (0x%08" PRIx32 ")\n\n",
           r->rand32, r->rand32);
}

static void print_devices(void)
{
    int count = tar_input_device_count();
    if (count < 0) {
        fprintf(stderr, "Errore elenco dispositivi: %s\n", tar_strerror());
        return;
    }

    printf("Dispositivi di input disponibili (%d):\n\n", count);
    for (int i = 0; i < count; ++i) {
        TarInputDevice dev;
        if (tar_get_input_device(i, &dev) != 0) {
            continue;
        }
        printf("  [%d] pa_index=%d  %s%s  (%s, %d canali)\n",
               i, dev.pa_index, dev.name,
               dev.is_default ? " [predefinito]" : "",
               dev.host_api_name, dev.max_input_channels);
    }
    printf("\nUsa: %s --device <pa_index> --count N\n", "main_test");
}

static void usage(const char *prog)
{
    printf("Uso:\n");
    printf("  %s                      genera 100000 campioni (default)\n", prog);
    printf("  %s --demo                mostra un ciclo completo del pipeline\n", prog);
    printf("  %s --list-devices        elenca i microfoni disponibili\n", prog);
    printf("  %s --device N            usa il dispositivo PortAudio con indice N\n", prog);
    printf("  %s --count N             genera N campioni\n", prog);
    printf("  %s --demo --count N      demo + generazione\n", prog);
}

int main(int argc, char *argv[])
{
    int do_demo = 0;
    int do_generate = 1;
    int do_list = 0;
    int count = 100000;
    int device_index = -1;
    const char *out_path = "random_samples.txt";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list-devices") == 0) {
            do_list = 1;
            do_generate = 0;
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_index = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--demo") == 0) {
            do_demo = 1;
            do_generate = 0;
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
            do_generate = 1;
            if (count <= 0) {
                fprintf(stderr, "Errore: --count richiede un numero positivo\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Opzione sconosciuta: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    printf("TrueAudioRand\n");
    printf("Assicurati che il microfono sia attivo e non muto.\n");

    if (do_list) {
        print_devices();
        return EXIT_SUCCESS;
    }

    int rc = tar_init_device(device_index);
    if (rc != 0) {
        fprintf(stderr, "tar_init() fallita (%d): %s\n", rc, tar_strerror());
        return EXIT_FAILURE;
    }

    if (do_demo) {
        TarPipelineReport report;
        memset(&report, 0, sizeof(report));
        rc = tar_demo_once(&report);
        if (rc != 0) {
            fprintf(stderr, "tar_demo_once() fallita (%d): %s\n", rc, tar_strerror());
            tar_close();
            return EXIT_FAILURE;
        }
        print_pipeline_report(&report);
    }

    if (do_generate) {
        if (do_demo) {
            printf("============================================================\n");
            printf("  GENERAZIONE CAMPIONI\n");
            printf("============================================================\n\n");
        } else {
            printf("\nGenerazione di %d campioni TRNG...\n\n", count);
        }

        FILE *out = fopen(out_path, "w");
        if (!out) {
            fprintf(stderr, "Impossibile creare %s\n", out_path);
            tar_close();
            return EXIT_FAILURE;
        }

        for (int i = 0; i < count; ++i) {
            uint32_t value = tar_get_rand32();
            fprintf(out, "%" PRIu32 "\n", value);

            if (count >= 1000 && (i + 1) % (count / 10 > 0 ? count / 10 : 1) == 0) {
                printf("  generati %d / %d campioni...\r", i + 1, count);
                fflush(stdout);
            }
        }

        printf("\nCompletato. Campioni salvati in %s\n", out_path);
        fclose(out);
    } else if (do_demo) {
        printf("\nPer generare campioni: %s --demo --count 1000\n", argv[0]);
        printf("Oppure usa: run_process.ps1\n");
    }

    tar_close();
    return EXIT_SUCCESS;
}
