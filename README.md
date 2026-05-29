
# HEP 3-Tier DAQ Control Center for CAEN DT5751

![Platform](https://img.shields.io/badge/Platform-Linux-blue)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)
![Python](https://img.shields.io/badge/Python-3.8+-3776AB?logo=python)
![ROOT](https://img.shields.io/badge/ROOT-6-black)
![License](https://img.shields.io/badge/License-MIT-green)

본 프로젝트는 입자 및 핵물리 실험(중성미자 탐색, 고속 유기/무기 섬광체 등)을 위한 **CAEN DT5751 디지타이저(10-bit, 1~2 GS/s) 전용 하이브리드 데이터 수집(DAQ) 시스템**입니다. 

기존 셸 스크립트 기반의 무거운 데이터 수집 파이프라인을 전면 폐기하고, **데이터 생산(C++)과 소비(Python)를 물리적으로 완벽히 분리(Decoupling)한 3-Tier 아키텍처**로 재설계되었습니다. 

특히 10-bit 해상도의 동적 범위(Dynamic Range)를 극대화하기 위한 시각화 툴, 런타임 환경 변수(수행자, 온도, HV)의 JSON DB 자동 푸쉬, 그리고 메모리 맵핑 기반의 **'타임머신 디버거(Time-Machine Debugger)'**가 탑재되어 상용 프로덕션 레벨 이상의 완벽한 사용자 경험(UX)과 데이터 무결성을 제공합니다.

---

## Directory Structure

```text
CPNR_dt5751/
├── CMakeLists.txt              # C++ 백엔드 및 GUI 자동 배포 빌드 스크립트
├── setup_env.sh                # 전역 환경 변수(PATH) 등록 셸 스크립트
├── README.md                   # 프로젝트 개요 및 가이드
│
├── config/                     # [Single Source of Truth] 장비 파라미터 및 메타데이터
│   ├── dt5751_inorganic_master.conf  # 무기 섬광체용 4us 윈도우 마스터 셋업
│   ├── dt5751_organic_ls_master.conf # 액체 섬광체(LS)용 512ns & 동시계수 셋업
│   └── dt5751_cli_test.conf          # CLI 코어 테스트용 기본 셋업
│
├── include/                    # [C++ 헤더] 공용 자료구조
│   ├── CaenDigitizer.h         # CAEN 하드웨어 제어 및 Zero-Copy 버퍼 래퍼
│   ├── ConfigParser.h          # UTF-8 NBSP 필터링 탑재 .conf 파서
│   ├── DAQManager.h            # 객체 지향 프론트엔드 코어
│   └── EventHeader.h           # 샘플링 속도(ps) 정보가 포함된 24 Bytes 초경량 헤더
│
├── src/                        # [Tier 1 & 2] 초고속 C++ 코어 엔진
│   ├── DAQManager.cpp          # (Tier 1) 하드웨어 개통, DES 모드 제어, ZMQ 스트리밍
│   ├── frontend_dt5751.cpp     # (Tier 1) 프론트엔드 독립 실행 CLI 진입점
│   └── production_dt5751.cpp   # (Tier 2) ROOT 변환기 및 타임머신 디버거(p/n/j/q)
│
├── gui/                        # [Tier 3] Python PyQt5 관제탑 (Edge Computing)
│   ├── main.py                 # dt5751gui 진입점
│   ├── core/                   # GUI 백그라운드 엔진
│   │   ├── DatabaseManager.py  # JSON 직렬화 메타데이터 및 런 서머리 푸쉬 엔진
│   │   └── ProcessManager.py   # 압축 로그 정규식 파서 및 QThread 백엔드 워커
│   ├── windows/                
│   │   └── MainWindow.py       # 6개 UX 탭 통합 컨테이너
│   └── widgets/                
│       ├── DaqTab.py           # 🚀 DAQ Control (퀵 액세스 메타데이터, 배치 컨트롤)
│       ├── EnvTab.py           # 🌡️ Environment Meta (사용자 정의 변수 JSON 직렬화)
│       ├── ConfigTab.py        # ⚙️ Hardware Config (10-bit 시뮬레이터 및 .conf 동기화)
│       ├── MonitorTab.py       # 📈 Live Monitor (다중 채널 자동 감지 ZMQ 오버레이)
│       ├── ProductionTab.py    # 🔬 Offline Production (ROOT 변환 통계 DB 연동)
│       └── DatabaseTab.py      # 🗄️ Run DB History (수행자 기반 측정 이력 통합 조회)
│
└── python_tools/               
    └── monitoring_dt5751.py    # X-Server 환경 없는 터미널 전용 CLI 라이브 모니터

```

---

## User Interface & Experience (UX)

### 1. DAQ Control & Quick Metadata

> 모던 라이트 테마(Light Theme)가 적용된 관제탑의 메인 패널입니다. **Operator(수행자), Applied HV(인가 전압), Temp(온도)** 등 측정의 핵심 메타데이터를 메인 화면에 전면 배치하여 직관성을 극대화했습니다. 수집된 통계(MB/s, Hz 등)는 수집 종료 시 자동으로 데이터베이스에 병합(Push)됩니다.

### 2. Environment Meta (NoSQL JSON Serialization)

> 관계형 스키마의 경직성을 탈피하기 위해 NoSQL 패러다임을 차용했습니다. 사용자가 런타임에 동적으로 환경 변수(습도, 케이블 길이 등)를 무한정 추가할 수 있으며, 이 데이터는 `DaqTab`의 핵심 정보와 병합되어 JSON 문자열로 DB에 영구 기록됩니다.

### 3. Hardware Config & 10-bit Dynamic Range Visualizer

> DT5751의 1Vpp, 10-bit(0~1023) 특성에 완벽하게 맞춰진 시뮬레이터입니다. 하드웨어 조준경(DCOffset, Threshold)을 표에서 즉시 편집하고 `.conf` 단일 진실 공급원(Single Source of Truth)으로 동기화합니다. 목표 베이스라인(%)과 트리거 깊이(mV)를 입력하면 16-bit 역방향 DAC 오프셋과 10-bit ADC 임계값을 역산출하여 렌더링합니다.

### 4. Live Monitor (Auto Multi-Channel Overlay)

> C++ 프론트엔드로부터 ZMQ 패킷을 수신하여 엣지 컴퓨팅을 수행합니다. 활성화된 다중 채널을 자동 감지하여 캔버스에 투명하게 오버레이(Overlay)하며, 사용자가 직접 누적 히스토리 사이즈(Events)를 동적으로 조절하여 시인성을 제어할 수 있습니다.

### 5. Offline Production (Time-Machine Debugger)

> `.dat` 이진 파일의 `.root` 물리 포맷 변환을 전담합니다.
> 특히 `std::map` 기반의 파일 포인터 매핑 기술이 적용된 **타임머신 디버거(-d)**를 탑재하여, 변환 중 터미널에서 이전 이벤트(p), 다음 이벤트(n), 특정 ID로 점프(j)하며 음극성 파형이 Y=0 베이스라인 기준으로 완벽하게 반전 정렬된 상태를 육안으로 검증할 수 있습니다. ROOT 변환 요약 데이터 역시 DB로 자동 푸쉬됩니다.

### 6. Run DB History (Data Provenance)

> JSON으로 직렬화된 환경 변수에서 **수행자(Operator)** 항목을 파서가 자동으로 추출해 전면 컬럼에 독립적으로 표출합니다. 언제, 누가, 어떤 고전압과 온도로 데이터를 수집했으며, 변환 속도와 수집 이벤트가 어떠했는지 모든 히스토리를 한눈에 추적합니다.

---

## System Architecture

1. **Tier 1: High-Speed Frontend (C++)**
* **역할:** 하드웨어 제어 및 Raw 데이터 초고속 기록.
* **특징:** 1 GS/s의 극한의 데이터 레이트를 감당하기 위해 **24 Bytes 초경량 헤더**와 순수 파형만 기록합니다. DES(Double Edge Sampling) 모드를 켤 경우 2 GS/s 속도로 동작 가능하도록 하드웨어 제어 로직이 내장되어 있습니다.


2. **Tier 2: Offline Production (C++ & ROOT)**
* **역할:** 데이터 포맷 변환 및 물리량(Charge, PHA, T0) 추출.
* **특징:** 10-bit 환경의 거친 양자화 노이즈를 뚫고 서브 나노초(Sub-ns) 단위의 펄스 시작점(T0)을 추출하며, 과거로 돌아갈 수 있는 디버깅 통신 인터페이스(sys/select.h)를 보유하고 있습니다.


3. **Tier 3: Control Center GUI (Python PyQt5)**
* **역할:** 통합 관제탑. 설정 파일 제어, ZMQ 수신 모니터링, 프로세스 라이프사이클 관리, SQLite 런 히스토리 통합.



---

## Prerequisites

* **OS:** Linux (CentOS 7, Rocky 8/9, Ubuntu 20.04+)
* **CAEN Libraries:** `CAENUSB`, `CAENVME`, `CAENComm`, `CAENDigitizer` (v1.0+)
* **Data & GUI Libraries:** ROOT 6 (C++17), ZeroMQ (`libzmq3-dev`), `PyQt5`, `pyqtgraph`, `numpy`

---

## Build & Installation

전역에서 시스템을 제어할 수 있도록 `setup_env.sh` 스크립트를 통한 셸 환경 변수(`$PATH`) 자동 등록 파이프라인이 포함되어 있습니다.

```bash
git clone [https://github.com/opercjy/CPNR_dt5751.git](https://github.com/opercjy/CPNR_dt5751.git)
cd CPNR_dt5751
mkdir build && cd build
cmake ..
make -j4

```

**[환경 변수 전역 등록]**
매번 `build/bin/` 폴더에 들어갈 필요 없이, 시스템 전역에서 `dt5751gui`를 호출하기 위해 아래 스크립트를 실행하십시오. (영구 등록을 원할 경우 `~/.bashrc`에 절대 경로를 추가하십시오.)

```bash
# 프로젝트 루트 디렉토리에서 실행
source setup_env.sh

```

---

## Usage

전역 환경 변수가 등록되었다면, 터미널 어디서든 아래 명령어로 관제탑을 가동할 수 있습니다.

```bash
dt5751gui

```

만약 백그라운드 데이터 변환 서버 등에서 CLI 변환기만 단독으로 사용하려면 아래와 같이 실행합니다.

```bash
production_dt5751 -i raw_data.dat -w

```

---


> **Open Source Statement**
> 본 프로젝트는 국가 기초과학 연구 인프라 확충과 공공의 이익에 기여하기 위해 전면 오픈소스로 공개됩니다. 
