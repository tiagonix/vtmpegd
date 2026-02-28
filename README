# VTmpeg Video Engine

VTmpeg is a high-performance, client-server video playback engine designed for Linux. It provides a robust daemon (`VTserver`) for rendering video via GStreamer and GTK 3, controlled by a lightweight command-line utility (`VTqueue`) via UNIX domain sockets.

This project is a modern rewrite of the original Void Technologies video engine, prioritizing gapless playback, thread-safe IPC, and minimal latency.

## Architecture

The system is divided into two decoupled components:

*   **`VTserver`**: The core engine. It manages a GStreamer 1.0 pipeline, renders video into a GTK 3 window, and hosts a UNIX domain socket server to process commands. It supports gapless transitions between queue items and features a "Station Standby" screen when idle.
*   **`VTqueue`**: The control client. A POSIX-compliant CLI tool that communicates with the server to manage the playback queue (listing, adding, and removing items).

## Requirements

### Build Dependencies
To build the engine, you need the development headers for GTK 3 and GStreamer 1.0.

**Debian/Ubuntu:**
```bash
sudo apt install build-essential pkg-config \
    libgtk-3-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev
```

### Runtime Dependencies
The server requires GStreamer plugins to decode various media formats. It is recommended to install the `good`, `bad`, and `ugly` sets for maximum compatibility.

```bash
sudo apt install gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
```

## Building the Project

The project uses a standard recursive Makefile structure.

*   **Build everything:** `make` or `make all`
*   **Clean build artifacts:** `make clean`

Executables will be generated in:
*   `src/server/VTserver`
*   `src/client/VTqueue`

## Usage

### Starting the Server
The server creates a window on the display and begins listening for commands on `/tmp/VTmpegd`.

```bash
./src/server/VTserver [OPTIONS]
```

**Options:**
*   `-l, --loop`: Enable playlist looping. When the queue is empty, the server restarts the last played item.
*   `-w, --watermark`: Enable the "VT-TV LIVE" watermark overlay on the video output.

### Managing the Queue
Use the `VTqueue` tool to control the server.

```bash
./src/client/VTqueue [OPTIONS]
```

**Common Commands:**
*   **Add a video:** `./VTqueue -a /path/to/video.mp4`
*   **Insert at position:** `./VTqueue -a /path/to/video.mp4 -p 2`
*   **List queue:** `./VTqueue -l`
*   **Remove item:** `./VTqueue -r 1`

## IPC Protocol Specification

Communication occurs over a UNIX domain socket using a simple text-based protocol.

| Command | ID | Arguments | Server Response |
| :--- | :--- | :--- | :--- |
| **List** | `1` | None | `S` (OK) + Queue List + `;` |
| **Insert** | `2` | `filename;pos` | `S` (OK) or `E` (Error) + `;` |
| **Remove** | `3` | `pos` | `S` (OK) or `E` (Error) + `;` |

*Note: The server uses the `S` (Success) and `E` (Error) characters followed by the `;` delimiter for all responses.*

## Project Structure

```text
.
├── src
│   ├── include
│   │   └── config.h      # Shared IPC definitions and constants
│   ├── server
│   │   ├── VTserver.c    # Main application loop and GTK setup
│   │   ├── unix.c        # UNIX Socket server and queue management
│   │   ├── gst-backend.c # GStreamer pipeline and gapless logic
│   │   ├── video.c       # GTK Drawing Area and XID embedding
│   │   └── commands.c    # Protocol command implementation
│   └── client
│       ├── VTqueue.c     # CLI argument parsing
│       └── cmd.c         # Socket communication logic
└── Makefile              # Top-level build orchestration
```

## License and Authors
Copyright (C) 2001-2002 Void Technologies (historical name, company closed).

*   **Alex Fiori** - Original Author
*   **Arnaldo Pereira** - Contributor
*   **Flávio Mendes** - Contributor
*   **Thiago Camargo** - Contributor, maintainer - Legacy code modernization, GStreamer 1.0 and GTK3 porting
