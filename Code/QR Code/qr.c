#include <stdbool.h>
#include <stdint.h>
#include "qrcodegen.h"
#include <stdlib.h>
#include "TinyPngOut.h"

int main()
{
    // Text data
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(
        "Hello, world!",
        tempBuffer,
        qr,
        qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN,
        qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO,
        true
    );

    if (!ok) return 0;

    FILE *file = fopen("out.png", "w");

    if(file==NULL)
    {
        printf("Could not open file\n");
        exit(-1);
    }

    // qr codes are square -> one int is enough to tell the size
    int size = qrcodegen_getSize(qr);
    printf("Size is: %d\n", size);

    struct TinyPngOut writer;
    enum TinyPngOut_Status init_status = TinyPngOut_init(&writer, size, size, file);

    if(init_status != TINYPNGOUT_OK)
    {
        printf("Could not initialize TinyPngOut writer. Status: %d\n", init_status);
        exit(-1);
    }

    uint8_t rgb_black[] = {0, 0, 0};
    uint8_t rgb_white[] = {255, 255, 255};

    for (int y = 0; y < size; y++) 
    {
        for (int x = 0; x < size; x++) 
        {
            bool color = qrcodegen_getModule(qr, x, y);

            TinyPngOut_write(&writer, color ? rgb_white: rgb_black, 1);
        }
    }

    return 0;
}