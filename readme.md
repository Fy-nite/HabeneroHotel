# ![](Hotones/habenero.png)


# Habenero

A social game engine built in C++ with Lua scripting, designed for creating multiplayer games with a focus on social interaction and fun. The engine provides a modular architecture, allowing developers to easily create and manage game content through asset packs and Lua scripts.


## Features

- Modular C++ engine with Lua scripting
- Asset pack system for easy content management
- Scene management (main menu, loading, in-game, transitions)
- Physics and collision system
- Audio system with sound bus
- Renderer with lighting and mesh support
- Extensible via custom scripts and packages

## Getting Started

### Prerequisites
- C++17 compatible compiler (MSVC, GCC, or Clang)
- CMake (if building from source)
- [raylib](https://www.raylib.com/) (if not bundled)
- Lua 5.4 (if not bundled)

### Build Instructions
1. Clone the repository:
	```sh
	git clone https://github.com/Fy-nite/Habenero.git
	```
2. Build the project:
    ```sh
    # on windows, use a ucrt64 shell from MSYS2 or similar
    cd Ho-Tones
    meow build
    ```
3. Run the engine:
	```sh
	./Hotones.exe
	```
	or run from the build directory as appropriate.

### Running the Demo
The DemoCupProject is included as an example. Place your assets and scripts in the appropriate folders under `DemoCupProject/` or `build/paks/DemoCupProject/`.

## Project Structure

- `Hotones/` - Main engine source code
  - `src/` - C++ source files
  - `include/` - Header files
  - `assets/` - Default assets (models, sounds, sprites)
  - `build/` - Build output and runtime files
  - `examples/` - Example code and demos
- `DemoCupProject/` - Example project (Lua scripts, models, etc.)
- `docs/` - Documentation and API references

## Contributing

Contributions are welcome! Please open issues or pull requests for bug fixes, new features, or suggestions. See the [wiki page](https://global.finite.ovh/wiki/doku.php?id=projects:habenero:lua) for API and engine documentation.

## License

This project is licensed under the AGPL License. See the LICENSE file for details.

## Credits

- Developed by Fy-nite
- Uses [raylib](https://www.raylib.com/) and [Lua](https://www.lua.org/)

For questions or support, open an issue or contact the maintainer.
