# Makefile for ChessGui — Qt6 + ONNX Runtime build on Apple Silicon (arm64)
# Bootstrap order (targets are independent wrappers, not a chain):
#   make install → make configure → make build, then make test / make run
SERVICE = ChessGui

# Variables
BUILD_DIR = build
BUILD_TYPE ?= Release
BREW_PREFIX ?= /opt/homebrew
APP_BIN = $(BUILD_DIR)/$(SERVICE)

.PHONY: help install configure build test run run-offscreen clean

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — Apple Silicon build\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

install: ## Install build deps via Homebrew (cmake, qtbase, qtsvg, onnxruntime) if missing
	@missing=""; \
	for f in cmake qtbase qtsvg onnxruntime; do \
		if brew list "$$f" >/dev/null 2>&1; then \
			echo "$$f already installed."; \
		else \
			missing="$$missing $$f"; \
		fi; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "Installing$$missing..."; \
		brew install $$missing; \
	fi
	@echo "Checking toolchain..."
	@cmake --version | head -1
	@$(BREW_PREFIX)/opt/qt6/bin/qmake --version 2>/dev/null | head -2 || qmake --version | head -2
	@uname -m
	@echo "Environment setup complete."

# ── Configure · Build ────────────────────────────────────────────────────────

configure: ## Configure out-of-source CMake build for arm64 (usage: make configure [BUILD_TYPE=Release|Debug])
	@echo "Configuring $(BUILD_TYPE) arm64 build in $(BUILD_DIR)..."
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH="$(BREW_PREFIX)"

$(BUILD_DIR)/CMakeCache.txt:
	@$(MAKE) configure

build: | $(BUILD_DIR)/CMakeCache.txt ## Compile ChessGui and test executables
	@echo "Building ChessGui..."
	cmake --build $(BUILD_DIR) --parallel

# ── Test · Run ───────────────────────────────────────────────────────────────

test: build ## Run the QtTest/CTest suite
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: | $(BUILD_DIR)/CMakeCache.txt ## Launch the desktop interface (native Cocoa)
	cmake --build $(BUILD_DIR) --parallel --target $(SERVICE)
	@echo "Launching $(APP_BIN)..."
	"$(APP_BIN)"

run-offscreen: QT_QPA_PLATFORM=offscreen ## Smoke-run headless (offscreen platform, no GUI)
run-offscreen: run
