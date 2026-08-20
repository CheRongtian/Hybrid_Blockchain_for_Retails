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

