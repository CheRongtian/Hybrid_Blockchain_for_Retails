#include <stdbool.h>
#include <stdint.h>
#include "qrcodegen.h"

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

    if (!ok)
        return 0;

    // qr codes are square -> one int is enough to tell the size
    int size = qrcodegen_getSize(qr);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            // (... paint qrcodegen_getModule(qr, x, y) ...)
            // somehow generate an image based on the module data
        }
    }

    return 0;
}