# QR Code
## Step 01 — Basic QR Code Generation
### Goal
Set up the QR Code Generator library and generate QR code data for a fixed text string.
At this stage, the program only creates the QR code data in memory. It does not display or save an image yet.

### Required Files
Place these files in the same directory:
* `qr.c`
* `qrcodegen.c`
* `qrcodegen.h`

`qrcodegen.c` and `qrcodegen.h` come from Project Nayuki's QR Code Generator library.

### Download
#### Linux
```bash
mkdir qr
cd qr
wget https://raw.githubusercontent.com/nayuki/QR-Code-generator/refs/heads/master/c/qrcodegen.c
wget https://raw.githubusercontent.com/nayuki/QR-Code-generator/refs/heads/master/c/qrcodegen.h
```

#### macOS / Unix
```bash
mkdir qr
cd qr
curl -O https://raw.githubusercontent.com/nayuki/QR-Code-generator/refs/heads/master/c/qrcodegen.c
curl -O https://raw.githubusercontent.com/nayuki/QR-Code-generator/refs/heads/master/c/qrcodegen.h
```

The QR code library itself is portable C, so macOS works fine. Only the download and compiler commands may differ from Linux.

### Build
#### Linux
```bash
gcc qr.c qrcodegen.c -o qr
```

Run:
```bash
./qr
```

#### macOS
```bash
clang qr.c qrcodegen.c -o qr
```

Run:
```bash
./qr
```

### Expected Result
The program should compile and exit normally without printing anything.
This is expected. Step 01 only generates the QR code data in memory. Image output will be added in later steps.

## Step 02 — First PNG Output

This step adds PNG output.

The QR code data generated in Step 01 is now converted into an actual image file named `out.png`.

The image is still only `21 × 21` pixels at this stage, so it will look very small.

### Additional Files

Download `TinyPngOut.c` and `TinyPngOut.h`.

Linux:
```bash id="7k0tr0"
wget https://www.nayuki.io/res/tiny-png-output/TinyPngOut.c
wget https://www.nayuki.io/res/tiny-png-output/TinyPngOut.h
```

macOS / Unix:
```bash id="xljvnk"
curl -O https://www.nayuki.io/res/tiny-png-output/TinyPngOut.c
curl -O https://www.nayuki.io/res/tiny-png-output/TinyPngOut.h
```

The project directory should now contain:

```text id="34x0sk"
qr/
├── qr.c
├── qrcodegen.c
├── qrcodegen.h
├── TinyPngOut.c
└── TinyPngOut.h
```

### Build and Run

Linux:
```bash id="rzodlh"
gcc -Wall -Wextra -g -o qr qr.c qrcodegen.c TinyPngOut.c
./qr
```

macOS:
```bash id="5fo7sc"
clang -Wall -Wextra -g -o qr qr.c qrcodegen.c TinyPngOut.c
./qr
```

### Expected Result

The program prints:
```text id="gnaxjx"
Size is: 21
```

A new file is also created:
```text id="2bvxox"
out.png
```

At this point, the QR code can be written as a PNG image, but the image is still only `21 × 21` pixels.
