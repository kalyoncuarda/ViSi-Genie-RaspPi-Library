![4D Systems](http://www.4dsystems.com.au/downloads/4DLogo.png)
ViSi-Genie-RaspPi-Library
=========================
4D Systems Raspberry Pi Library for Visi-Genie

Library for the Raspberry Pi to allow easy communication between 4D Intelligent Display modules running ViSi-Genie programmed from Workshop 4, and the Raspberry Pi.
This library is also required for the Raspberry Pi demo programs.

## Available Versions

This repository now includes **three** versions of the library. Pick the one that fits your project:

| Folder / File | Language | Description |
|---|---|---|
| `geniePi.c` + `geniePi.h` (root) | C | Original 4D Systems library. Kept as-is for reference / backwards compatibility with existing projects. |
| `headerandcpp/` (`GeniePiLib.h` + `GeniePiLib.cpp`) | C++ | Same functionality wrapped in a `GeniePi` class (RAII, `std::thread`/`std::mutex` instead of raw `pthread`). Split into header + source, compiled as a normal library. **Recommended for new projects.** |
| `headeronly/` (`GeniePi.h`) | C++ | Identical to the above, but everything is `inline` in a single header. Drop the file into your project and `#include` it — no separate compilation step needed. |

All three versions implement the same protocol, timing (5ms/50ms/10s), and object/command set. The two C++ versions additionally contain the following bug fixes over the original C library:

* **Reply listener desync fix:** in the original code, a timeout while reading a `MAGIC_BYTES`/`DOUBLE_BYTES` reply used `continue` inside the *inner* byte-reading loop instead of aborting the whole reply, which could desync the serial protocol parser. The C++ versions now correctly abort and resync on timeout.
* **`genieWriteMagicBytes` / `genieWriteDoubleBytes` length bug:** the original code computed the array length with `sizeof(pointer) / sizeof(int)`, which does not give the real array length (a pointer's `sizeof` is fixed, e.g. 8 bytes). The length is now computed by counting elements up to the null terminator, matching how the array is actually sent.
* Serial-port related counters and queue indices (`genieReplysHead`, `genieReplysTail`, `genieChecksumErrors`, `genieTimeouts`) use `unsigned int` instead of `int`, since they can never be negative.

### Usage (C++ versions)

```cpp
#include "GeniePiLib.h"   // or "GeniePi.h" for the header-only version

GeniePi genie;
genie.genieSetup("/dev/ttyUSB0", 115200);
genie.genieWriteObj(GENIE_OBJ_ILED_DIGITS_L, 0, 1234);
```

## Genie Pi version history

### v1.4 (C++ versions added)
* Added `headerandcpp/` (header + cpp) and `headeronly/` (single header) C++ wrappers around a `GeniePi` class.
* Fixed reply-listener desync bug on timeout during magic/double-byte replies.
* Fixed `genieWriteMagicBytes` / `genieWriteDoubleBytes` incorrect length calculation.
* Original C library (`geniePi.c` / `geniePi.h`) kept unchanged at the repo root as a reference/legacy version, with `int` → `unsigned int` corrected on internal counters and queue indices only.

### v1.3
* Added the following function:
  ```
  genieWriteShortToIntLedDigits (int index, int16_t data)
  genieWriteLongToIntLedDigits  (int index, int32_t data)
  genieWriteFloatToIntLedDigits (int index, float data)
  ```
* Added numerous new objects to support Internal/Inherent Widgets in Workshop4

### v1.2
* Added the following function:
  ```
  genieWriteStrHex  (int index, long n)
  genieWriteStrDec  (int index, long n)
  genieWriteStrOct  (int index, long n)
  genieWriteStrBin  (int index, long n)
  genieWriteStrBase (int index, long n, int base)
  genieWriteStrFloat(int index, float n, int precision)
  ```

### v1.1
* Added the following function:
  ```
  genieWriteMagicBytes  (int magic_index, unsigned int *byteArray)
  genieWriteDoubleBytes (int magic_index, unsigned int *doubleByteArray)
  ```
* Added additional struct: `genieMagicReplyStruct` : `cmd`, `index`, `length`, `data[100]`

## Dependencies

This section discusses the package requirements of the library.

### WiringPi
* Connect your Raspberry Pi to the Internet and install wiringPi:
  ```
  sudo apt install wiringpi
  ```
* Please see here for more details: https://projects.drogon.net/raspberry-pi/wiringpi/download-and-install/

## Installation

This section discusses install and uninstall procedure for the original C library (`geniePi.c`/`geniePi.h`).

### Install Genie Pi Library
```
make
sudo make install
```

### Uninstall Genie Pi Library
```
sudo make uninstall
```

For the C++ versions, either compile `headerandcpp/GeniePiLib.cpp` into your project's build, or simply `#include "headeronly/GeniePi.h"`.

## Setup Raspberry Pi Serial UART hardware

* In a default install of Raspbian, the primary UART is assigned to the Linux console. Using the serial port for other purposes requires this default behaviour to be changed. On startup, systemd checks the Linux kernel command line for any console entries, and will use the console defined therein. To stop this behaviour, the serial console setting needs to be removed from the command line.
* This can be done using the `raspi-config` utility, or manually.
  ```
  sudo raspi-config
  ```
* Select **Interfacing options**, then option **Serial**
* Select **No** to disable Console via Serial
* Select **Yes** to enable Serial hardware
* Exit Raspberry Pi Configuration
* To manually change the settings, edit the kernel command line with:
  ```
  sudo nano /boot/cmdline.txt
  ```
* Find the console entry that refers to the serial0 device, and remove it, including the baud rate setting. It will look something like: `console=serial0,115200`
* Make sure the rest of the line remains the same, as errors in this configuration can stop the Raspberry Pi from booting.
* Reboot the Raspberry Pi for the change to take effect.

## Questions/Issues?
Please sign up for our Forum and ask a question there, or submit a Tech Support Ticket from our website.
http://forum.4dsystems.com.au or http://www.4dsystems.com.au/support
