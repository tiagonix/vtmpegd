# VTmpeg Video Engine

VTmpeg is a high-performance, client-server video playback engine designed for Linux. It provides a robust daemon (`VTserver`) for rendering video via GStreamer and GTK 3, controlled by a lightweight command-line utility (`VTqueue`) via UNIX domain sockets.

This project is a modern rewrite of the original Void Technologies video engine, prioritizing gapless playback, thread-safe IPC, and minimal latency.

## Architecture

The system is divided into two decoupled components:

*   **`VTserver`**: The core engine. It manages a GStreamer 1.0 pipeline, renders video into a GTK 3 window, and hosts a UNIX domain socket server to process commands. It supports gapless transitions between queue items and features a "Station Standby" screen when idle.
*   **`VTqueue`**: The control client. A POSIX-compliant CLI tool that communicates with the server to manage the playback queue (listing, adding, and removing items).

### Queue Behavior

The server is hardened with a **2048-item limit** to prevent memory exhaustion and ensure stability. The queue management logic depends on the `--loop` flag:
*   **Default (Station Mode):** The queue operates as a FIFO (First-In, First-Out). Videos are removed from the queue after they are played, allowing for continuous, long-term operation without manual cleanup.
*   **Loop Mode (`-l`, `--loop`):** The queue is treated as a persistent playlist. Videos remain in the queue after playback, and the server cycles back to the first item upon reaching the end.

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
*   **Show Playback Status:** `./VTqueue --status` (or `-s`)
*   **Pause Playback:** `./VTqueue --pause` (or `-P`)
*   **Resume Playback:** `./VTqueue --resume` (or `-R`)
*   **Stop Playback:** `./VTqueue --stop` (or `-S`)

## IPC Protocol Specification

Communication occurs over a UNIX domain socket using a simple text-based protocol.

| Command | ID | Arguments | Server Response | Description |
| :--- | :--- | :--- | :--- | :--- |
| **List** | `1` | None | `S` + List + `;` | Lists the current video queue. |
| **Insert** | `2` | `file;pos` | `S` or `E` + `;` | Inserts a video (pos 0 for end). |
| **Remove** | `3` | `pos` | `S` or `E` + `;` | Removes the video at the given position. |
| **Play** | `4` | None | `S` or `E` + `;` | Resumes playback. |
| **Pause** | `5` | None | `S` or `E` + `;` | Pauses playback. |
| **Stop** | `6` | None | `S` or `E` + `;` | Stops playback and shows standby screen. |
| **Next** | `7` | None | `S` or `E` + `;` | Skips to the next video in the queue. |
| **Prev** | `8` | None | `S` or `E` + `;` | Returns to the previous video in the queue. |
| **Mute** | `9` | None | `S` or `E` + `;` | Toggles audio output on/off. |
| **Status** | `10` | None | `S` + Info + `;` | Gets playback status and progress. |

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
│   │   ├── commands.c    # Protocol command implementation
│   │   └── thread.c      # Thread management helpers
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
