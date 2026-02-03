#!/usr/bin/env bash
# ~/repos/kwin/build.sh - KWin build automation script

set -e  # Exit on error

# Configuration
SRC_DIR="/home/all/repos/kwin"
BUILD_DIR="/home/all/repos/kwin/builddir"
INSTALL_PREFIX="/usr"
CMAKE_PREFIX_PATH="/home/all/kde/usr;/usr"
mkdir -p "$BUILD_DIR"
# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
CLEAN=false
BUILD=true
INSTALL=false
TESTS=false
JOBS=$(nproc)
BUILD_TYPE="Debug"

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build script for KWin

OPTIONS:
    clean           Clean build directory before building
    rebuild         Clean and build
    install         Build and install
    all             Clean, build, install, and run tests
    --tests         Enable building and running tests
    --release       Build in Release mode (default: Debug)
    --jobs N        Number of parallel build jobs (default: $(nproc))
    -h, --help      Show this help message

EXAMPLES:
    $0              # Just build
    $0 clean build  # Clean then build
    $0 install      # Build and install
    $0 all --tests  # Full rebuild with tests
    $0 rebuild --release --jobs 8
EOF
    exit 0
}

log() {
    echo -e "${GREEN}==>${NC} ${BLUE}$1${NC}"
}

warn() {
    echo -e "${YELLOW}Warning:${NC} $1"
}

error() {
    echo -e "${RED}Error:${NC} $1" >&2
    exit 1
}

clean() {
    log "Cleaning build directory..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"/*
        log "Build directory cleaned"
    else
        warn "Build directory doesn't exist yet"
    fi
}

configure() {
    log "Configuring KWin..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    CMAKE_ARGS=(
        "$SRC_DIR"
        -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DKWIN_BUILD_X11=ON
    )
    
    if [ "$TESTS" = false ]; then
        CMAKE_ARGS+=(-DBUILD_TESTING=OFF)
        log "Tests disabled"
    else
        CMAKE_ARGS+=(-DBUILD_TESTING=ON)
        log "Tests enabled"
    fi
    
    cmake "${CMAKE_ARGS[@]}"
}

build() {
    log "Building KWin with $JOBS parallel jobs..."
    cd "$BUILD_DIR"
    
    if [ ! -f "CMakeCache.txt" ]; then
        configure
    fi
    
    cmake --build . -j"$JOBS" || error "Build failed"
    log "Build completed successfully"
}

install() {
    log "Installing KWin to $INSTALL_PREFIX..."
    cd "$BUILD_DIR"
    cmake --install . || error "Installation failed"
    log "Installation completed successfully"
}

run_tests() {
    log "Running tests..."
    cd "$BUILD_DIR"
    ctest --output-on-failure -j"$JOBS" || warn "Some tests failed"
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    BUILD=true
fi

while [ $# -gt 0 ]; do
    case "$1" in
        clean)
            CLEAN=true
            BUILD=false
            ;;
        build)
            BUILD=true
            ;;
        rebuild)
            CLEAN=true
            BUILD=true
            ;;
        install)
            BUILD=true
            INSTALL=true
            ;;
        all)
            CLEAN=true
            BUILD=true
            INSTALL=true
            TESTS=true
            ;;
        --tests)
            TESTS=true
            ;;
        --release)
            BUILD_TYPE="Release"
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --jobs)
            shift
            JOBS="$1"
            ;;
        -j*)
            JOBS="${1#-j}"
            ;;
        -h|--help)
            usage
            ;;
        *)
            error "Unknown option: $1 (use --help for usage)"
            ;;
    esac
    shift
done

# Main execution
echo ""
log "KWin Build Script"
log "Source:  $SRC_DIR"
log "Build:   $BUILD_DIR"
log "Install: $INSTALL_PREFIX"
log "Type:    $BUILD_TYPE"
echo ""

[ "$CLEAN" = true ] && clean
[ "$BUILD" = true ] && build
[ "$INSTALL" = true ] && install
[ "$TESTS" = true ] && run_tests

log "All done! ✨"
echo ""
log "To use your custom KWin:"
echo "  source $BUILD_DIR/prefix.sh"
echo "  kwin_wayland --replace &"
echo ""
