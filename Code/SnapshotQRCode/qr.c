#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qrcodegen.h"
#include "TinyPngOut.h"

#define SCALE 10
#define QUIET_ZONE_MODULES 4
#define QR_MAX_INPUT_LENGTH 2953

static void strip_line_ending(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

int main(int argc, char **argv)
{
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [text] [output.png]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char inputText[QR_MAX_INPUT_LENGTH + 1] = {0};
    if (argc >= 2) {
        size_t inputLength = strlen(argv[1]);
        if (inputLength == 0 || inputLength > QR_MAX_INPUT_LENGTH) {
            fprintf(stderr, "QR Code content must contain between 1 and %d characters.\n",
                    QR_MAX_INPUT_LENGTH);
            return EXIT_FAILURE;
        }
        memcpy(inputText, argv[1], inputLength + 1);
    } else {
        if (fgets(inputText, sizeof(inputText), stdin) == NULL) {
            fprintf(stderr, "No QR Code content was provided.\n");
            return EXIT_FAILURE;
        }
        strip_line_ending(inputText);
        if (inputText[0] == '\0') {
            fprintf(stderr, "No QR Code content was provided.\n");
            return EXIT_FAILURE;
        }
    }

    const char *outputPath = argc >= 3 ? argv[2] : "out.png";
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool encoded = qrcodegen_encodeText(
        inputText,
        tempBuffer,
        qr,
        qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN,
        qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO,
        true
    );
    if (!encoded) {
        fprintf(stderr, "The QR Code content could not be encoded.\n");
        return EXIT_FAILURE;
    }

    FILE *file = fopen(outputPath, "wb");
    if (file == NULL) {
        fprintf(stderr, "Could not open QR Code output file: %s\n", outputPath);
        return EXIT_FAILURE;
    }

    int size = qrcodegen_getSize(qr);
    int imageSize = (size + QUIET_ZONE_MODULES * 2) * SCALE;
    struct TinyPngOut writer;
    enum TinyPngOut_Status status = TinyPngOut_init(
        &writer,
        (uint32_t)imageSize,
        (uint32_t)imageSize,
        file
    );
    if (status != TINYPNGOUT_OK) {
        fprintf(stderr, "Could not initialize QR Code PNG writer. Status: %d\n", status);
        fclose(file);
        return EXIT_FAILURE;
    }

    const uint8_t rgbBlack[] = {0, 0, 0};
    const uint8_t rgbWhite[] = {255, 255, 255};
    for (int y = 0; y < imageSize; ++y) {
        for (int x = 0; x < imageSize; ++x) {
            int moduleX = x / SCALE - QUIET_ZONE_MODULES;
            int moduleY = y / SCALE - QUIET_ZONE_MODULES;
            bool dark = moduleX >= 0 && moduleX < size &&
                        moduleY >= 0 && moduleY < size &&
                        qrcodegen_getModule(qr, moduleX, moduleY);
            status = TinyPngOut_write(&writer, dark ? rgbBlack : rgbWhite, 1);
            if (status != TINYPNGOUT_OK) {
                fprintf(stderr, "Could not write QR Code PNG. Status: %d\n", status);
                fclose(file);
                return EXIT_FAILURE;
            }
        }
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Could not finalize QR Code output file: %s\n", outputPath);
        return EXIT_FAILURE;
    }

    printf("A QR Code has been generated: %s\n", outputPath);
    return EXIT_SUCCESS;
}
