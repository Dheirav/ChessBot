# Source Notes

The project is a desktop chess application rather than a reusable library. Build and run it from the repository root:

```bash
make
./chessbot
```

The main entry point is [src/main.cpp](../src/main.cpp), which initializes the GUI, creates the engine, and starts the game loop. The engine logic lives under [src/engine](engine), and the SFML UI lives under [src/gui](gui).

Useful maintenance commands:

```bash
make clean
make remake
```