# QLogger

A lightweight, thread-safe logging library for Qt applications.

QLogger is a small drop-in logging module built on top of Qt's native message
system. It installs a custom `qInstallMessageHandler` so that regular
`qDebug()`, `qInfo()`, `qWarning()` and `qCritical()` calls are automatically
formatted, filtered and written to rotating log files — no changes to your
existing logging code required.

It is intentionally compact (one header + one source file) and has no
dependencies other than Qt Core, making it easy to embed in desktop, console or
embedded Qt projects.

## Features

- **Native Qt integration** — works with `qDebug` / `qInfo` / `qWarning` /
  `qCritical`, including `QLoggingCategory`.
- **Synchronous or asynchronous writing** — async mode buffers entries on a
  queue and flushes them on a timer or when the queue grows large.
- **Log level filtering** — configurable minimum level
  (`Debug`, `Info`, `Warning`, `Critical`, `Fatal`).
- **Automatic file rotation** — rotates by size and cleans up old files.
- **Flexible formatting** — timestamp, thread ID, category and source
  location (for debug messages).
- **Thread-safe** — the message handler may be called concurrently from any
  thread.
- **Runtime reconfiguration** — change directory, file name, level or mode
  while the application is running.
- **Qt 5 and Qt 6 support** (C++17).

## Requirements

- CMake 3.16 or newer
- C++17
- Qt 5.15+ or Qt 6 (Core; Widgets only for the demo application)

## Project structure

```
QLogger/
├── qlogger/                  # Core library
│   ├── qlogger.h
│   └── qlogger.cpp
├── logger_test_widget.h/.cpp # GUI demo / manual test application
├── main.cpp                  # Demo entry point
├── tests/                    # Qt Test based unit tests
│   ├── CMakeLists.txt
│   └── tst_qlogger.cpp
└── CMakeLists.txt
```

## Quick start

### Option 1: Use it as a CMake subdirectory (recommended)

The repository exposes a `QLogger` static library target:

```cmake
add_subdirectory(path/to/QLogger)

target_link_libraries(YourApp PRIVATE QLogger)
```

Then include the header:

```cpp
#include "qlogger/qlogger.h"
```

### Option 2: Copy the sources

Copy `qlogger/qlogger.h` and `qlogger/qlogger.cpp` into your project and link
against Qt Core.

## Usage

```cpp
#include <QApplication>
#include <QStandardPaths>

#include "qlogger/qlogger.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    LogConfig config;
    config.logDir = QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation) + "/logs";
    config.logFileName = "my_app";
    config.maxFileSize = 5 * 1024 * 1024;
    config.maxFileCount = 5;
    config.minLevel = LogLevel::Info;
    config.enableConsole = true;
    config.enableTimestamp = true;
    config.enableThreadId = true;
    config.asyncMode = true;
    config.flushInterval = 1000;

    if (!initLogger(config)) {
        // Failed to create the log directory or open the log file.
    }

    // Standard Qt logging is now captured by QLogger.
    qInfo() << "Application started";
    qDebug() << "This is ignored because minLevel is Info";

    // Convenience macros append to the "app" category.
    LOG_INFO("User logged in");
    LOG_WARNING("Cache is almost full");

    // Category-specific convenience macros.
    LOG_NETWORK_INFO("Connected to server");

    // ... application logic ...

    shutdownLogger();  // flush and close
    return 0;
}
```

> **Note:** The async flush timer is created on the thread that calls
> `initLogger()`. Initialize the logger from a thread with a running Qt event
> loop (normally the main thread) when `asyncMode` is enabled.

## Configuration

| Field             | Default    | Description                                        |
| ----------------- | ---------- | -------------------------------------------------- |
| `logDir`          | `"logs"`   | Directory for log files (created if missing).      |
| `logFileName`     | `"app_log"`| File name prefix.                                  |
| `maxFileSize`     | `10 MiB`   | Rotate when the active file exceeds this size.     |
| `maxFileCount`    | `5`        | Number of rotated files to keep.                   |
| `minLevel`        | `Debug`    | Minimum level to write.                            |
| `enableConsole`   | `true`     | Also print to stdout/stderr.                       |
| `enableTimestamp` | `true`     | Include a millisecond timestamp.                   |
| `enableThreadId`  | `false`    | Include the current thread ID.                     |
| `asyncMode`       | `false`    | Buffer entries and flush on a timer.               |
| `flushInterval`   | `1000`     | Async flush interval in milliseconds.              |

## Log files

The active file is named `<prefix>_yyyyMMdd.log`. When it exceeds
`maxFileSize`, it is renamed to `<prefix>_yyyyMMdd_hhmmss_zzz.log` (with a
numeric suffix if two rotations happen in the same millisecond) and a new
active file is opened. `cleanupOldFiles()` keeps the newest `maxFileCount`
rotated files and never deletes the active file.

## Building the demo

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/QLoggerDemo.app/Contents/MacOS/QLoggerDemo   # macOS bundle
```

## Running the tests

The test suite uses Qt Test and covers initialization, level filtering,
formatting, synchronous and asynchronous writes, rotation, reconfiguration,
concurrent writes and shutdown.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Thread safety

`QLogger` is a singleton and all file access is guarded by a mutex. The message
handler can safely be called from multiple threads. Configuration changes are
serialized with logging, so `setConfig()` may be called at runtime (for example
from a settings dialog).

## License

No license has been specified yet. If you plan to use QLogger in an open-source
project, add a `LICENSE` file (for example MIT or Apache-2.0) before
distributing it.
