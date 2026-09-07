
# ChessGui

A modern, fast, and lightweight desktop Chess GUI built with **C++20** and **Qt 6**.

ChessGui offers an interactive chessboard interface, automatic screenshot-to-FEN recognition, complete chess rule validation, real-time classical heuristic position evaluation, UCI chess engine integration (local processes or remote engines behind a chessgateway server), a dynamic evaluation bar, live PGN move history, and automatic OS-standard configuration management.

<img width="1062" height="1039" alt="image" src="https://github.com/user-attachments/assets/3f6f24f6-ee97-471f-92de-f7e4688d5849" />

---

## Features

- **Interactive Chessboard UI**
  - Crisp vector rendering using third-party SVG pieces (Chessnut set).
  - Smooth piece interaction with both click-to-move and drag-and-drop support.
  - Visual hints for selected squares, hovered squares, and legal move destinations.
  - Automatic board resizing with responsive aspect ratio preservation.
  - The board is a read-only view of the position: it reports move intent and is refreshed by the game controller, which owns the authoritative state.

- **Complete Chess Rules Engine**
  - Clean, UI-independent C++20 rules implementation (`Rules`).
  - Full FIDE rule support: legal move generation, check detection, checkmate, and stalemate.
  - Special moves: kingside & queenside castling, *en passant* captures, and pawn promotions (including interactive underpromotions to Queen, Rook, Bishop, or Knight with visual icons and keyboard shortcuts).

- **Real-Time Heuristic Position Evaluation**
  - Self-contained classical evaluator (`HeuristicEval`) with no external model files required.
  - Combines material, piece-square tables, mobility, pawn structure, passed pawns, king safety, bishop pair, rook activity, and game phase.
  - Computes positional centipawn scores and a display percentage from White's perspective.
  - Automatically updates on each legal move.

- **External UCI Chess Engine Integration**
  - Connect to any UCI-compatible chess engine (Stockfish, Komodo, etc.) via asynchronous process communication (`UciEngine`).
  - MultiPV principal variations table, depth, selective depth, node counts, NPS, search time, and best move reporting (`EngineOutputWidget`).
  - Real-time UCI log console and engine start/pause/stop controls.

- **Remote Engines via <a href="https://github.com/Morditux/chessgateway">chessgateway</a>**
  - `Engine -> Remote Engine...` opens a dialog to enter the gateway server address and port and query its available engines (`List available engines`).
  - The dialog talks to the server with the `chessgateway/1` JSON Lines protocol over TCP (`ChessGatewayClient`); the engine executable path never leaves the server.
  - The dialog also offers an optional access key: when the server requires client authentication (hello feature `access_keys`), the client authenticates with this key before listing engines or selecting one. An empty key on an authentication-requiring server is reported as a clear error.
  - The selected engine (host, port, engine id, name, version, and optional access key) is saved in the configuration file, replacing any local engine selection (and vice versa).
  - The remote engine is loaded as soon as it is selected (the gateway session starts immediately, so its UCI options are active without starting an analysis), and the same `GameController` flows (analysis, timed search, game state machine) drive it as they do a local engine.
  

- **Opening Book**
  - Uses the bundled `assets/books/book.bin` Crafty opening book through the `Book` class.
  - Applies book moves at the start of games against the computer and sends the resulting move list to the UCI engine when engine search begins.

- **Dynamic Evaluation Bar**
  - Compact vertical widget (`EvaluationBar`) indicating winning probability in real-time.
  - Smooth proportions, 50% equilibrium center marker, and dynamic evaluation tooltip.

- **PGN & FEN Management (Games Menu)**
  - Start a new game against the currently loaded UCI engine with PGN tags, side selection, and standard time controls (2h, 1h, 30m, 15m, or 5m blitz per player).
  - Load full PGN game files (`Load PGN...`) with automatic move replay, check/mate validation, and live move history update.
  - Copy current FEN (`Ctrl+Shift+F`) or full PGN (`Ctrl+Shift+C`) directly to the clipboard.
  - Paste any FEN position directly from the clipboard using `Ctrl+V` or the `Games -> Paste FEN or screenshot` menu action.
  - Import a chessboard screenshot from the clipboard, a browser drag, or a file-manager drag. The board is detected, classified with ONNX Runtime, and loaded into the chessboard automatically.
  - Use the visible `White to play` checkbox to choose the side to move when a screenshot does not contain game-state metadata.
  - Complete round-trip FEN export and SAN (Standard Algebraic Notation) disambiguation.

- **Screenshot-to-FEN Recognition**
  - Detects an axis-aligned 8x8 chessboard in a larger screenshot.
  - Classifies the 64 squares with the bundled `chess-tiles-v2.onnx` model.
  - Displays confidence and orientation feedback, while retaining a manual FEN fallback.
  - Detection and classification run in a dedicated worker thread (`VisionWorker`), so pasting or dropping a screenshot never freezes the UI; a request counter discards stale results when several images are processed in a row.

- **PGN Move History & Navigation**
  - Automatic algebraic notation generator supporting piece markers, capture markers (`x`), checks (`+`), checkmates (`#`), castling (`O-O` / `O-O-O`), and promotions (`=Q`).
  - Split-view layout allowing seamless game recording and review.
  - Full game navigation: Step backward/forward (`Left` / `Right`) and jump to start/end (`Home` / `End`, toolbar buttons, and menu actions).

- **UI-Independent Game Controller**
  - `GameController` owns the authoritative position, PGN history, UCI engine, opening book, heuristic evaluator, and the computer-game state machine.
  - It also owns the `ChessGatewayClient` used for remote engines and exposes a normalized engine state (`isEngineConnected`, `isEngineAnalyzing`, `engineState`) so the UI does not care whether the engine is local or remote.
  - `MainWindow` only wires widgets to controller signals, keeping the game logic fully testable without a GUI.

- **OS-Standard Configuration Management**
  - QSettings-backed INI configuration (`AppConfig`), auto-created at startup if missing.
  - Stored in the OS-standard configuration directory (`~/.config/ChessGui/` on Linux, `AppData/Local/ChessGui/` on Windows, `Library/Application Support/ChessGui/` on macOS).
  - Automatically persists the chosen UCI engine path, the selected remote engine (gateway host, port, engine id, optional access key), MainWindow position and size, and all splitter layout sizes.

---

## Project Structure

```
ChessGui/
├── CMakeLists.txt                 # CMake build configuration and test targets
├── LICENSE                        # GNU GPL version 2 license for ChessGui
├── main.cpp                       # Application entry point
├── mainwindow.h / .cpp            # Main application window & split layout (UI wiring only)
├── gamecontroller.h / .cpp        # UI-independent game controller (position, PGN, engine, computer game)
├── computergamedialog.h / .cpp    # New-game PGN and time-control dialog
├── computergamesettings.h / .cpp  # ComputerGameSettings struct shared by dialog and controller
├── chessboard.h / .cpp            # Chessboard widget (SVG rendering & input handling)
├── chessboard.ui                  # Qt Designer UI form for ChessBoard
├── promotiondialog.h / .cpp       # Pawn promotion piece selection dialog (Queen, Rook, Bishop, Knight)
├── rules.h / .cpp                 # Standalone chess rules & move validation engine
├── heuristiceval.h / .cpp         # Self-contained classical position evaluator
├── evaluationbar.h / .cpp         # Vertical evaluation bar widget
├── pendulumwidget.h / .cpp        # Chess clock widgets
├── boarddetector.h / .cpp          # Screenshot chessboard detection
├── fenrecognizer.h / .cpp          # ONNX Runtime tile classification to FEN
├── visionworker.h / .cpp           # Detection/recognition worker on a dedicated thread
├── appconfig.h / .cpp             # OS-standard configuration manager (chessGui.conf)
├── uciengine.h / .cpp             # UCI engine communication and process control
├── gatewayclient.h / .cpp         # chessgateway/1 JSON Lines client for remote engines
├── remoteenginedialog.h / .cpp    # Remote engine configuration dialog (server query & selection)
├── uciparser.h / .cpp             # UCI protocol parser and data structures
├── book.h / .cpp                  # Crafty opening-book reader and move selection
├── craftyhashkeys.h               # Crafty-compatible opening-book hash keys
├── engineoutputwidget.h / .cpp    # UCI engine analysis output & control widget
├── pieces.qrc                     # Qt Resource file embedding SVGs
├── assets/
│   ├── icons/chessgui.png         # Generated modern application icon
│   ├── models/chess-tiles-v2.onnx  # Screenshot tile-classification model
│   ├── books/book.bin              # Crafty opening book
│   └── pieces/                    # Chessnut SVG piece set & Apache 2.0 license
├── THIRD_PARTY_NOTICES.md          # Fenshot and tensorflow_chessbot notices
├── packaging/
│   └── chessgui.desktop           # Linux desktop launcher metadata
├── tests/
│   ├── rules_test.cpp             # Unit tests for Rules, FEN, SAN, and PGN loading
│   ├── heuristiceval_test.cpp     # Unit tests for heuristic evaluation
│   ├── evaluationbar_test.cpp     # Unit tests for EvaluationBar widget
│   ├── pendulumwidget_test.cpp    # Unit tests for PendulumWidget clocks
│   ├── uciparser_test.cpp         # Unit tests for UCI parser
│   ├── book_test.cpp              # Unit tests for opening-book lookup
│   ├── uciengine_test.cpp         # Unit tests for UCI engine integration
│   ├── gatewayclient_test.cpp     # Unit tests for the chessgateway/1 client
│   ├── remoteenginedialog_test.cpp# Unit tests for the remote engine dialog
│   ├── engineoutputwidget_test.cpp# Unit tests for EngineOutputWidget
│   ├── appconfig_test.cpp         # Unit tests for application configuration management
│   ├── gamecontroller_test.cpp    # Unit tests for the game controller
│   ├── chessboard_test.cpp        # Unit tests for chessboard widget and user interactions
│   ├── promotiondialog_test.cpp   # Unit tests for promotion selection dialog
│   └── vision_test.cpp            # Unit tests for board detection and FEN recognition
├── AGENTS.md                      # Guidelines and conventions for development agents
└── README.md                      # Project documentation
```

---

## Prerequisites

- **CMake** 3.16 or higher
- **C++20 Compiler** (GCC 10+, Clang 11+, or MSVC 2019+)
- **Qt 6.8+** modules:
  - `Qt6Core`
  - `Qt6Gui`
  - `Qt6Network`
  - `Qt6Widgets`
  - `Qt6Svg`
  - `Qt6Test` (optional, for running test suites)
- **ONNX Runtime** 1.23+ development package (`libonnxruntime-dev` on Ubuntu/Debian)
- **Qt Linguist tools** (`qt6-tools-dev-tools` on Ubuntu/Debian, optional): only needed to compile translation files once a `translations/` directory with `.ts` files is added

### Installing Dependencies (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-svg-dev libonnxruntime-dev libgl1-mesa-dev
```

### Installing Dependencies (Arch Linux)

```bash
sudo pacman -S base-devel cmake qt6-base qt6-svg
```

---

## Building and Running

### 1. Build the Application

Configure and build out-of-source with CMake:

```bash
cmake -S . -B build
cmake --build build --parallel
```

### 2. Run the Application

Launch the desktop interface:

```bash
./build/ChessGui
```

For headless environments (e.g. CI/CD or virtual frames), specify the offscreen platform:

```bash
QT_QPA_PLATFORM=offscreen ./build/ChessGui
```

### Creating an installable Debian package

The CMake configuration enables CPack's DEB generator on Linux. After configuring and building:

```bash
cpack --config build/CPackConfig.cmake
```

This creates `chessgui_1.0.5_amd64.deb` in the project root. Install it with:

```bash
sudo apt install ./chessgui_1.0.5_amd64.deb
```

The package installs the `ChessGui` executable, its desktop launcher, the generated application
icon, the GPL-2.0 license, and the runtime Qt dependencies detected by CPack.

---

## Downloads

The latest successful Windows x64 portable build is published automatically by
the Windows release workflow:

[Download ChessGui for Windows x64](https://github.com/Morditux/ChessGuiNG/releases/download/windows-latest/ChessGui-windows-x64.zip)

## Running Tests

ChessGui includes automated unit tests built with QtTest and integrated with CTest.

To run all unit tests:

```bash
ctest --test-dir build --output-on-failure
```

Or run individual test suites directly:

```bash
./build/RulesTest
./build/HeuristicEvalTest
./build/EvaluationBarTest
./build/PendulumWidgetTest
./build/UciParserTest
./build/BookTest
./build/UciEngineTest
./build/GatewayClientTest
./build/RemoteEngineDialogTest
./build/EngineOutputWidgetTest
./build/AppConfigTest
./build/GameControllerTest
./build/VisionTest
```

### Test Coverage

- **`RulesTest`**: Validates starting/midgame/endgame FEN parsing, invalid FEN detection, FEN round-trip generation, SAN move parsing with disambiguation/promotions/castling, PGN file/string parsing, and MainWindow FEN paste / PGN loading.
- **`HeuristicEvalTest`**: Validates balanced opening evaluation, insufficient material, centipawn-to-display-percentage formulas, passed pawns, piece advantage, Fool's Mate, Scholar's Mate, pawn-shelter rewards for castled kings, check penalty for the side to move, and drawn single-minor endgames.
- **`EvaluationBarTest`**: Validates evaluation bar property clamping, color customisation, signal emission, size constraints, and offscreen widget rendering.
- **`UciParserTest`**: Validates parsing of UCI `info` lines (depth, seldepth, score cp/mate, nodes, NPS, time, MultiPV, PV), move conversion, and win percentage mappings.
- **`UciEngineTest`**: Validates process lifecycle, mock UCI engine protocol exchange, MainWindow engine integration, and the eager remote-engine load (MainWindow configured with a gateway host connects and loads the engine at startup, so its UCI options are active immediately).
- **`GatewayClientTest`**: Validates the `chessgateway/1` client against a fake gateway server (hello handshake, engine listing, engine selection, UCI command forwarding, UCI output events, protocol errors, and disconnection), including client authentication: a valid access key authenticates and allows listing engines, a missing or invalid key is reported and the connection is closed.
- **`RemoteEngineDialogTest`**: Validates the remote engine dialog (initial state, access key field, Ok-button enablement, engine listing against a fake gateway — including an authentication-requiring gateway — and empty engine lists).
- **`EngineOutputWidgetTest`**: Validates analysis summary formatting, MultiPV table updating, UCI log console, and engine controls.
- **`AppConfigTest`**: Validates OS-standard configuration file path discovery, auto-creation of `chessGui.conf` at startup, persistence of window geometry/size/position, splitter sizes, remote engine settings, and chosen UCI engine loading.
- **`GameControllerTest`**: Validates FEN/PGN loading, SAN/UCI move recording and move numbering, illegal-move rejection, side-to-move toggling, opening-book moves, a full computer game against a mock UCI engine (move previews, timed search reply), engine disconnection ending the game, and the eager remote-engine connection (loaded as soon as it is selected) for analyses and computer games.
- **`VisionTest`**: Validates invalid-image handling and ONNX model initialization. Set `CHESSGUI_SAMPLE_IMAGE` to an image path to run an optional end-to-end detector/classifier check.

---

## License

ChessGui's project-specific source code is licensed under the GNU General Public
License, version 2 only (GPL-2.0-only). See [LICENSE](LICENSE).

The bundled third-party assets and adapted components retain their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the Chessnut
[Apache License 2.0](assets/pieces/LICENSE-2.0.txt).

## Third-Party Assets & Credits

- **Chess Pieces**: The SVG piece icons are from the [Chessnut](https://github.com/lichess-org/lila/tree/master/public/piece/chessnut) set by Alexis Luengas, licensed under the [Apache License 2.0](assets/pieces/LICENSE-2.0.txt).
- **Screenshot recognition**: The board detector and `chess-tiles-v2.onnx` model are adapted from [Fenshot](https://github.com/scoriiu/fenshot) and the MIT-licensed [tensorflow_chessbot](https://github.com/Elucidation/tensorflow_chessbot). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
