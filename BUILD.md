# Build Instructions - ClosedHoodVisualizer

> [!WARNING]
> This project depends on **JUCE 8**, which requires **Microsoft Visual Studio 2022** on Windows.
> **MinGW / GCC is explicitly NOT supported.**

## 1. Prerequisites
- **Visual Studio 2022**: Install "Desktop development with C++" workload.
- **CMake**: Version 3.15 or higher.
- **Git**: To fetch the JUCE submodule.

## 2. Build Instructions (Visual Studio)

### Option A: Using CMake (Recommended)
1.  Open a terminal (PowerShell or Command Prompt).
2.  Navigate to the project directory:
    ```powershell
    cd ClosedHoodVisualizer
    ```
3.  Configure the project:
    ```powershell
    cmake -B build
    ```
    *This will automatically find your Visual Studio installation and use the MSVC compiler.*
4.  Build the project:
    ```powershell
    cmake --build build --config Release
    ```
5.  **Run Standalone App (for testing)**:
    ```powershell
    # Launch the standalone executable to test audio/visuals
    ./build/ClosedHoodVisualizer_artefacts/Release/Standalone/ClosedHood Visualizer.exe
    ```

### Option B: Visual Studio IDE
1.  Open the folder `ClosedHoodVisualizer` in Visual Studio 2022.
2.  Visual Studio will automatically detect `CMakeLists.txt` and configure the project.
3.  Select `ClosedHoodVisualizer - Release` from the target dropdown.
4.  Click **Build** -> **Build All**.

## 3. Artifacts
After a successful build, you will find:
- **VST3 Plugin**: `build/ClosedHoodVisualizer_artefacts/Release/VST3/ClosedHood Visualizer.vst3`
- **Standalone App**: `build/ClosedHoodVisualizer_artefacts/Release/Standalone/ClosedHood Visualizer.exe`
