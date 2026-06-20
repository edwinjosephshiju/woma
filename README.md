# WomaPython

WomaPython is a revolutionary AI-powered compiler built directly inside the CPython core. Instead of executing standard Python scripts, Woma natively transpiles natural language "logic" files (`.woma`) into standard Python bytecode on-the-fly and executes them securely within the robust CPython standard library environment. 

By statically embedding a `1.5GB` language model using the advanced `llama.cpp` neural engine directly into the C executable, Woma operates entirely offline with zero external dependencies, maintaining the structural guarantees and execution speed of standard CPython!

## Installation (Pre-built Releases)

The easiest way to install WomaPython is by using the pre-built installer packages available on the [Releases page](https://github.com/edwinjosephshiju/woma/releases). The installers will automatically set up Woma and download the required AI models.

### Windows
1. Download `woma-windows-x86_64-installer.exe` from the latest release.
2. Run the installer and follow the prompt. It will automatically download the 1.28GB Qwen model and add `woma` to your system `PATH`.
3. Open a new PowerShell window and run `woma` to start the REPL.

### Linux (Debian/Ubuntu)
1. Download `woma-linux-x86_64.deb` from the latest release.
2. Install the package using `apt`:
   ```bash
   sudo apt install ./woma-linux-x86_64.deb
   ```
3. Run `woma` from your terminal to start the REPL.

## Building from Source

If you prefer to compile Woma natively, follow these instructions.

### Requirements
*   Linux (or WSL2) environment
*   `gcc` / `g++` 
*   `make`
*   `cmake`
*   `curl` (to fetch the 1.5GB Qwen2.5-Coder model dynamically during build)
*   Standard Python dependencies (`zlib1g-dev`, `libssl-dev`, etc.)

## Build Instructions

1.  **Clone the Repository**
    ```bash
    git clone https://github.com/edwinjosephshiju/WomaPython.git
    cd WomaPython
    git submodule update --init --recursive
    ```

2.  **Configure the Build System**
    ```bash
    ./configure
    ```

3.  **Compile the Native Engine**
    ```bash
    make -j$(nproc)
    ```
    *Note: During this step, the Woma Makefile will automatically download the LLM `model.gguf` using `curl` if it is not present, invoke CMake to statically build `llama.cpp` with disabled shared-libraries, and statically link the neural network and `libllama.a` into the final `woma` executable.*

## Source Installation

To deploy the Woma transpiler system-wide after building from source:

```bash
sudo make install
```
*This installs the `woma` compiler command securely alongside `python3.12` in your `/usr/local/bin` folder, fully hooking it up to the standard libraries.*

## Usage

Create a new file containing plain English logic, for example `logic.woma`:
```text
Print "Hello World".

If variable A is greater than 1 then print "A is not zero" else print "A is zero".
```

Run the compiler anywhere natively:
```bash
woma logic.woma
```

### Inner Workings
Woma includes an optimized **C-level prompt injection filter** that actively scans for and violently rejects "chatbot" syntax (e.g., "Write a script that..."). This ensures Woma operates purely as a compiler—translating strict logical intent into Python AST before compiling it natively!
