# Project Guide: Botanik Greenhouse Control System

This guide provides a comprehensive overview of the Botanik project, a control system for a small-scale horticultural greenhouse. It's designed to help developers quickly understand the project's structure, technologies, and development workflow.

## 1. Project Overview

Botanik is a control system for a small-scale horticultural greenhouse, managing irrigation, ventilation, and monitoring of autonomous systems (emergency heat blower, heatpump) and power supply. The system is currently implemented using an Arduino OPTA PLC, allowing for custom C++ code execution to overcome limitations of the Arduino PLC platform.

**Key Technologies:**
*   **Programming Languages:** C, C++
*   **Build System:** CMake
*   **Target Platforms:**
    *   **SIMULATOR:** For development and testing on a standard Linux environment.
    *   **OPTA:** For deployment on the Arduino OPTA PLC (ARM Cortex-M7).
*   **Terminal Interaction:** ANSI escape codes for rich terminal output and input editing.

**High-level Architecture:**
The project is structured around a core `libbotanik` C++ library that handles the control logic, terminal interaction, logging, and hardware abstraction. A `botanik_sim` executable provides a simulator environment for development. The `CMakeLists.txt` handles compilation for both simulator and the actual OPTA PLC target, including specific toolchains and libraries for the embedded environment.

## 2. Getting Started

### Prerequisites
*   CMake (version 3.10 or higher)
*   A C++ compiler (g++ recommended)
*   For OPTA target: `arm-none-eabi-gcc` and `arm-none-eabi-g++` toolchain.
*   (Assumed) Arduino IDE and associated tools for OPTA deployment.

### Installation Instructions

1.  **Clone the repository:**
    ```bash
    git clone [repository_url]
    cd botanik
    ```

2.  **Build for Simulator (Development):**
    ```bash
    mkdir build
    cd build
    cmake -DBOTANIK_TARGET=SIMULATOR ..
    make
    ```
    This will generate the `botanik_sim` executable.

3.  **Build for OPTA (Deployment - Sanity Check):**
    ```bash
    mkdir build_opta
    cd build_opta
    cmake -DBOTANIK_TARGET=OPTA ..
    make
    ```
    This build configuration is primarily for a sanity check and does not produce a deployable binary directly due to the nature of Arduino PLC objects.

### Basic Usage Examples

**Running the Simulator:**
After building for the `SIMULATOR` target:
```bash
./botanik_sim
```
(Further usage details will depend on the `botanik_sim` implementation.)

### Running Tests
(Currently, no explicit testing framework or instructions are present in the provided codebase. This section will need to be updated as tests are added.)

## 3. Project Structure

The project is organized into the following main directories and key files:

*   **`.continue/`**: Contains configuration files for the Continue IDE extension.
*   **`.git/`**: Git version control system files.
*   **`.vscode/`**: VS Code editor configuration files.
*   **`images/`**: Project images (e.g., `logo.png`).
*   **`src/`**: Source code for the project.
    *   **`src/botanik_sim/`**: Contains the main entry point for the simulator executable (`main.cpp`).
    *   **`src/libbotanik/`**: The core C++ library for the control system.
        *   **`src/libbotanik/hal/`**: Hardware Abstraction Layer.
            *   `opta.h`: OPTA-specific hardware definitions (assumed).
            *   `platform.h`: General platform definitions.
            *   `simulator.h`: Simulator-specific hardware definitions (assumed).
        *   **`src/libbotanik/logs/`**: Logging utilities.
            *   `logger.h`, `logger.cpp`: Core logging functionality.
            *   `writer.h`, `writer.cpp`: Log writing mechanisms.
        *   **`src/libbotanik/panes/`**: (Directory found, but contents not analyzed. Assumed to relate to UI panes for terminal display).
        *   **`src/libbotanik/plcio/`**: PLC I/O related functionalities.
            *   `emulator.h`, `emulator.cpp`: Emulation of PLC I/O for the simulator.
            *   `logs.h`, `logs.cpp`: Logging related to PLC I/O.
            *   `sync.h`, `sync.cpp`: Synchronization mechanisms for PLC I/O.
            *   `vars.h`: PLC variable definitions (assumed).
        *   **`src/libbotanik/terminal/`**: Terminal interaction and UI components.
            *   `ansi.h`, `ansi.cpp`: ANSI escape code definitions and helper functions for terminal control.
            *   `editor.h`, `editor.cpp`: Multi-line input editor with ripple effects.
            *   `parser.h`, `parser.cpp`: Terminal command parsing (assumed).
            *   `terminal.h`, `terminal.cpp`: High-level terminal abstraction.
        *   **`src/libbotanik/utils/`**: Utility functions.
            *   `serial.h`: Serial communication utilities.
            *   `time.h`: Time-related utilities.
    *   **`src/libbotanik.h`**: Main header for the `libbotanik` library, including other component headers.
    *   **`src/libbotanik_config.h`**: (File found, but contents not analyzed. Assumed to contain configuration macros.)
*   **`CMakeLists.txt`**: The primary CMake build script, defining project structure, targets, and platform-specific compilation flags.
*   **`README.md`**: Project README file with a high-level description.

## 4. Development Workflow

### Coding Standards or Conventions
*   **Language:** C++11 standard is enforced by CMake (`CMAKE_CXX_STANDARD 11`).
*   **Style:** (Not explicitly defined in the provided files. Developers should agree on a consistent coding style, e.g., Google C++ Style Guide or a similar established standard.)
*   **Documentation:** Doxygen-style comments are used in some headers (e.g., `editor.h`) for API documentation.

### Testing Approach
Currently, there is no dedicated testing framework or explicit unit tests defined in the codebase. Development relies on the `SIMULATOR` target for functional verification. Future development should incorporate a testing strategy (e.g., Google Test, Catch2) for robust unit and integration testing.

### Build and Deployment Process
*   **Build:** Use CMake as described in "Getting Started" for either `SIMULATOR` or `OPTA` targets.
*   **Deployment (OPTA):** The `OPTA` build configuration performs a "sanity check" compilation using the `arm-none-eabi-gcc` toolchain. Actual deployment to the Arduino OPTA PLC would likely involve integrating the compiled code with the Arduino IDE or a similar platform-specific deployment mechanism.

### Contribution Guidelines
(Not explicitly defined. It is recommended to establish clear guidelines for branching, commit messages, pull requests, and code reviews.)

## 5. Key Concepts

*   **Arduino OPTA PLC:** The target hardware platform, a Programmable Logic Controller that allows running custom C++ code.
*   **Modbus RTU:** (Mentioned in `README.md`) A serial communication protocol used for communication with I/O modules.
*   **ANSI Escape Codes:** Used for controlling terminal output (cursor movement, text formatting, screen manipulation) for the command-line interface.
*   **Ripple Algorithm:** (Described in `editor.h`) An intended optimization for terminal updates, minimizing serial bandwidth by using hardware-accelerated insertion/deletion of lines/characters. This is particularly relevant for embedded systems with limited communication throughput.
*   **Hardware Abstraction Layer (HAL):** The `src/libbotanik/hal` directory suggests a layer to abstract platform-specific hardware interactions, allowing the core logic to be more portable.
*   **Prompts (PS1, PS2):** Standard prompt conventions in command-line interfaces. PS1 is the primary prompt, while PS2 is used for continuation lines in multi-line input.

## 6. Common Tasks

### Building the Simulator
```bash
mkdir build
cd build
cmake -DBOTANIK_TARGET=SIMULATOR ..
make
```

### Running the Simulator
```bash
./botanik_sim
```

### Cleaning the Build
```bash
cd build # or build_opta
make clean
```
or to remove the entire build directory:
```bash
rm -rf build build_opta
```

### Debugging
*   For the `SIMULATOR` target, standard debugging tools (e.g., GDB with VS Code integration) can be used. The `DEBUG` macro is defined for the simulator build.
*   For the `OPTA` target, debugging will depend on the specific capabilities of the Arduino OPTA PLC and its development environment (e.g., serial logging, JTAG/SWD debugging if supported).

## 7. Troubleshooting

*   **"Undefined reference" errors during OPTA build:** This might occur if the necessary Arduino/Mbed libraries and PLC objects are not correctly linked or available in the CMake environment. The `OPTA` target build is currently described as a "sanity check" and might not be fully linked for a complete binary.
*   **Terminal display issues:** If the terminal output appears corrupted or not as expected, ensure that the terminal emulator supports ANSI escape codes. Incorrect implementation of the ripple algorithm (once implemented) could also lead to visual glitches.
*   **CMake configuration errors:** Double-check the `CMakeLists.txt` for correct paths and variable definitions, especially when switching between `SIMULATOR` and `OPTA` targets.

## 8. References

*   [Arduino OPTA Documentation](https://www.arduino.cc/pro/hardware/product/opta-wifi) (Link to official documentation, assumed)
*   [CMake Documentation](https://cmake.org/documentation/)
*   [ANSI Escape Codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
*   [Modbus Protocol Specification](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf) (Assumed relevant documentation)

