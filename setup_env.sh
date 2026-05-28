#!/bin/bash
# ==============================================================================
# CPNR_dt5751 Environment Setup Script
# ==============================================================================

# 스크립트가 위치한 절대 경로를 역산출하여 bin 디렉토리 매핑
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BIN_DIR="${PROJECT_ROOT}/build/bin"

# 빌드 디렉토리 존재 유무 검증
if [ ! -d "$BIN_DIR" ]; then
    echo "[Error] Directory $BIN_DIR does not exist."
    echo "Please run 'cmake ..' and 'make' in the build directory first."
    # source로 실행되었는지 확인 후 적절히 종료
    return 1 2>/dev/null || exit 1
fi

# PATH 환경 변수에 중복 등록되는 것을 방지하는 방어 로직
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    export PATH="$BIN_DIR:$PATH"
    echo "[System] Successfully appended to PATH: $BIN_DIR"
else
    echo "[System] PATH is already configured for: $BIN_DIR"
fi

echo "[System] Global commands now available:"
echo "  - dt5751gui"
echo "  - frontend_dt5751"
echo "  - production_dt5751"