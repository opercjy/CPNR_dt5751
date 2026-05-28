cmake_minimum_required(VERSION 3.17)
project(CAEN_DT5751_3TIER_DAQ CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 초당 GB 단위 데이터를 감당하기 위한 극한의 최적화
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -pthread -fPIC")

# [형상 관리 철학] 실행 파일 및 배포 환경을 무조건 build/bin 하위로 격리
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 프로젝트 루트에 필수 GUI 뼈대 폴더 강제 생성 (복사 에러 방지)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/gui/core
                    ${CMAKE_SOURCE_DIR}/gui/widgets
                    ${CMAKE_SOURCE_DIR}/gui/windows)

# 데이터 저장 폴더 및 설정 파일 폴더를 build 내부에 생성
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config)

# 설정 파일 동기화 (원본 소스의 config -> build/bin/config)
file(COPY ${CMAKE_SOURCE_DIR}/config/
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config/
     FILES_MATCHING PATTERN "*.conf")

# 필수 시스템 패키지 탐색 (libusb-1.0 커널 독립성 확보)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# ROOT 프레임워크 탐색
find_package(ROOT QUIET COMPONENTS Core Tree Hist RIO Rint Gpad Graf Gui)

include_directories(include ${ZMQ_INCLUDE_DIRS} ${LIBUSB_INCLUDE_DIRS})

# 시스템 헤더 파일 리스트화 (의존성 추적 및 IDE 인덱싱 목적)
set(HEADERS 
    include/CaenDigitizer.h
    include/ConfigParser.h
    include/DAQManager.h
    include/EventHeader.h
)

# ------------------------------------------------------------------------------
# [Tier 1] High-Speed Frontend (CLI Core)
# ------------------------------------------------------------------------------
add_executable(frontend_dt5751 src/frontend_dt5751.cpp src/DAQManager.cpp ${HEADERS})
target_link_libraries(frontend_dt5751 
    ${ZMQ_LIBRARIES} 
    ${LIBUSB_LIBRARIES} 
    CAENDigitizer CAENComm CAENVME pthread)

# ------------------------------------------------------------------------------
# [Tier 2] Offline Production (ROOT)
# ------------------------------------------------------------------------------
if(ROOT_FOUND)
  include(${ROOT_USE_FILE})
  add_executable(production_dt5751 src/production_dt5751.cpp ${HEADERS})
  target_link_libraries(production_dt5751 ${ROOT_LIBRARIES})
else()
  message(WARNING "ROOT framework not found. Skipping 'production_dt5751' build.")
endif()

# ------------------------------------------------------------------------------
# [Tier 3] GUI Deployment Automation
# ------------------------------------------------------------------------------
# 빌드 성공 시 원본 gui 폴더를 통째로 build/bin/gui 로 복사
add_custom_command(TARGET frontend_dt5751 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/gui ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/gui
    COMMENT "Deploying Python GUI edge computing modules to build/bin directory..."
)

# GUI 구동을 위한 래퍼 셸 스크립트(dt5751gui) 자동 생성 및 실행 권한 부여
file(WRITE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dt5751gui "#!/bin/bash\ncd \"\$(dirname \"\$0\")\"\npython3 gui/main.py \"\$@\"\n")
execute_process(COMMAND chmod +x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dt5751gui)