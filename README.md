# vsng-motion-telemetry
UDP Telemetry output for Virtual Sailor NG (VSNG) for 6DOF Motion Platforms.

# Virtual Sailor NG - UDP Motion Telemetry

This is a custom telemetry extractor for **Virtual Sailor NG (VSNG)**. It extracts internal physics data (Pitch, Roll, Heave, Speed, etc.) from the game's memory and sends it via UDP to motion simulator software like **FlyPT Mover** or **SimTools** for 6-DOF Motion Platforms.

## Features
- Direct memory hooking via C++ DLL.
- Outputs floating-point telemetry data.
- Sends data over standard UDP port `4444`.
- Compatible with Thanos AMC-AASD15A controllers and heavy-duty actuators.

## Files Included
- `dllmain.cpp`: The main C++ source code to be compiled as a DLL plugin for VSNG.
- `vsng_mem.py`: An alternative Python script using `pymem` to read memory and send UDP packets externally (without injecting a DLL).

## How to Use (DLL Method)
1. Compile `dllmain.cpp` into a `.dll` (e.g., `vsng_telemetry.dll`) using Visual Studio.
2. Place the compiled `.dll` into the root folder of Virtual Sailor NG.
3. The DLL will automatically initialize when the game starts and broadcast telemetry data to `127.0.0.1:4444`.

## FlyPT Mover Setup
1. Add a **Source :: Custom UDP** module.
2. Set port to `4444`.
3. Set Data Type to `Float (4 bytes)`.
4. Map the UDP bytes to your Pose axes (Pitch, Roll, Yaw, Heave).

---
*Created for a 1-Ton Commercial Rescue Boat Simulator Project.*
