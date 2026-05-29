#include "DAQManager.h"
#include "EventHeader.h"
#include <CAENComm.h> 

#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <thread>
#include <zmq.h>
#include <filesystem>

DAQManager::DAQManager(const std::string &config_file, const std::string &output_file,
                       int max_events, int run_time_sec)
    : config_(config_file), output_file_(output_file), max_events_(max_events),
      run_time_sec_(run_time_sec), running_(false),
      digitizer_(CAEN_DGTZ_USB, 0, 0, 0), current_sample_rate_ps_(1000)
{
    zmq_ctx_ = zmq_ctx_new();
    zmq_pub_ = zmq_socket(zmq_ctx_, ZMQ_PUB);
    int hwm = 5000;
    zmq_setsockopt(zmq_pub_, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    
    // 🌟 [핵심 해결] ZMQ_LINGER = 0 설정!
    // 남은 패킷에 미련을 갖지 않고 즉시 컨텍스트를 파괴하도록 하여 좀비 프로세스화를 원천 차단합니다.
    int linger = 0;
    zmq_setsockopt(zmq_pub_, ZMQ_LINGER, &linger, sizeof(linger));
    
    zmq_bind(zmq_pub_, "tcp://127.0.0.1:5555");

    if (!output_file_.empty()) {
        std::filesystem::path p(output_file_);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        
        // 파일 쓰기(std::ios::out) 모드를 추가하여 파일이 생성되지 않던 현상 해결
        out_stream_.open(output_file_, std::ios::out | std::ios::binary);
        if (!out_stream_.is_open()) {
            std::cerr << "\n\033[1;31m[Fatal Error]\033[0m Cannot open output file: " << output_file_ << "\n";
            throw std::runtime_error("File permission denied or invalid path.");
        }
    }
    SetupHardware();
}

DAQManager::~DAQManager() {
    Stop();
    if (out_stream_.is_open()) {
        out_stream_.flush();
        out_stream_.close();
    }
    zmq_close(zmq_pub_);
    zmq_ctx_destroy(zmq_ctx_);
}

void DAQManager::SetupHardware() {
    std::cout << "\033[1;36m[DAQManager]\033[0m Configuring DT5751 Hardware from Config...\n";
    int handle = digitizer_.GetHandle();
    
    int des_mode = config_.GetInt("Digitizer", "EnableDESMode", 0);
    uint32_t channel_mask = config_.GetInt("Digitizer", "ChannelMask", 0x0F);
    
    if (des_mode > 0) {
        CAEN_CHECK(CAEN_DGTZ_SetDESMode(handle, CAEN_DGTZ_ENABLE));
        current_sample_rate_ps_ = 500; 
        channel_mask &= 0x05;         
        std::cout << " -> DES Mode ENABLED (2 GS/s). CH1 & CH3 are masked out.\n";
    } else {
        CAEN_CHECK(CAEN_DGTZ_SetDESMode(handle, CAEN_DGTZ_DISABLE));
        current_sample_rate_ps_ = 1000; 
        std::cout << " -> DES Mode DISABLED (1 GS/s).\n";
    }

    uint32_t record_length = config_.GetInt("Digitizer", "RecordLength", 1024);
    CAEN_CHECK(CAEN_DGTZ_SetRecordLength(handle, record_length));
    CAEN_CHECK(CAEN_DGTZ_SetChannelEnableMask(handle, channel_mask));
    CAEN_CHECK(CAEN_DGTZ_SetPostTriggerSize(handle, config_.GetInt("Digitizer", "PostTrigger", 80)));

    int sync_mode = config_.GetInt("Synchronization", "RunSyncMode", 0);
    if (sync_mode == 1) {
        CAEN_CHECK(CAEN_DGTZ_SetRunSynchronizationMode(handle, CAEN_DGTZ_RUN_SYNC_TrgOutTrgInDaisyChain));
        std::cout << " -> Run Synchronization: DAISY CHAIN (TRG-IN/OUT) ENABLED.\n";
    } else {
        CAEN_CHECK(CAEN_DGTZ_SetRunSynchronizationMode(handle, CAEN_DGTZ_RUN_SYNC_Disabled));
    }

    int pol_val = config_.GetInt("Digitizer", "TriggerPolarity", 1); 
    
    for (int ch = 0; ch < MAX_DT5751_CH; ++ch) {
        if ((channel_mask >> ch) & 1) {
            std::string ch_sec = "Channel_" + std::to_string(ch);
            uint32_t offset = config_.GetInt(ch_sec, "DCOffset", 32768); 
            uint32_t thr = config_.GetInt(ch_sec, "TriggerThreshold", 500);
            
            if (thr > 1023) {
                std::cerr << "\033[1;33m[Warning] CH" << ch << " Threshold (" << thr << ") exceeds 10-bit limit. Forced to 1023.\033[0m\n";
                thr = 1023;
            }
            CAEN_CHECK(CAEN_DGTZ_SetChannelDCOffset(handle, ch, offset));
            CAEN_CHECK(CAEN_DGTZ_SetTriggerPolarity(handle, ch, (pol_val == 0) ? CAEN_DGTZ_TriggerOnRisingEdge : CAEN_DGTZ_TriggerOnFallingEdge));
            CAEN_CHECK(CAEN_DGTZ_SetChannelTriggerThreshold(handle, ch, thr));
        }
    }

    CAEN_DGTZ_TriggerMode_t trg_mode = CAEN_DGTZ_TRGMODE_ACQ_ONLY;
    if (config_.GetInt("Digitizer", "ExtTriggerMode", 0) > 0) {
        CAEN_CHECK(CAEN_DGTZ_SetExtTriggerInputMode(handle, trg_mode));
    } else {
        CAEN_CHECK(CAEN_DGTZ_SetExtTriggerInputMode(handle, CAEN_DGTZ_TRGMODE_DISABLED));
    }

    if (config_.GetInt("Digitizer", "SelfTriggerMode", 1) > 0) {
        CAEN_CHECK(CAEN_DGTZ_SetChannelSelfTrigger(handle, trg_mode, channel_mask));
    } else {
        CAEN_CHECK(CAEN_DGTZ_SetChannelSelfTrigger(handle, CAEN_DGTZ_TRGMODE_DISABLED, 0x0F));
    }

    CAEN_CHECK(CAEN_DGTZ_SetSWTriggerMode(handle, trg_mode));
    CAEN_CHECK(CAEN_DGTZ_SetAcquisitionMode(handle, CAEN_DGTZ_SW_CONTROLLED));

    digitizer_.AllocateBuffers();
    size_t max_safe_size = sizeof(EventHeader) + (record_length + 1024) * sizeof(uint16_t) * MAX_DT5751_CH;
    raw_buffer_pool_.resize(max_safe_size);
}

void DAQManager::Start(std::atomic<bool>& is_running) {
    CAEN_CHECK(CAEN_DGTZ_SWStartAcquisition(digitizer_.GetHandle()));
    AcquisitionLoop(is_running);
}

void DAQManager::Stop() {
    running_ = false;
}

void DAQManager::AcquisitionLoop(std::atomic<bool>& is_running) {
    EventHeader *header = reinterpret_cast<EventHeader *>(raw_buffer_pool_.data());
    uint16_t *wave_dest = reinterpret_cast<uint16_t *>(raw_buffer_pool_.data() + sizeof(EventHeader));

    int handle = digitizer_.GetHandle();
    char *caen_buffer = digitizer_.GetReadoutBuffer();
    CAEN_DGTZ_UINT16_EVENT_t *caen_event = digitizer_.GetDecodedEvent();

    uint32_t event_count = 0;
    uint32_t prev_ttt = 0;
    uint64_t ttt_rollovers = 0;
    const uint32_t TTT_MASK = 0x7FFFFFFF;

    auto start_wall = std::chrono::system_clock::now();
    auto start_time = std::chrono::steady_clock::now();
    auto last_log_time = start_time;

    uint32_t log_events = 0;
    uint32_t zmq_drops = 0;
    size_t total_bytes_written = 0; 
    size_t last_bytes_written = 0; 

    std::cout << "\n\033[1;32m[DAQ Started]\033[0m Press Ctrl+C to stop gracefully." << std::endl;

    while (is_running) {
        auto now = std::chrono::steady_clock::now();

        // 시간 및 이벤트 리미트 검사를 루프 최상단에 배치
        if (max_events_ > 0 && (int)event_count >= max_events_) {
            std::cout << "\n\033[1;33m[System] Event Limit Reached. Stopping...\033[0m" << std::endl;
            break;
        }
        if (run_time_sec_ > 0) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= run_time_sec_) {
                std::cout << "\n\033[1;33m[System] Time Limit Reached. Stopping...\033[0m" << std::endl;
                break;
            }
        }

        double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time).count();
        
        // 1초마다 무조건 시계 강제 갱신
        if (elapsed_ms >= 1000.0) {
            double rate = (log_events / elapsed_ms) * 1000.0;
            double speed_mbps = ((total_bytes_written - last_bytes_written) / 1048576.0) / (elapsed_ms / 1000.0);
            last_bytes_written = total_bytes_written;
            
            auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            int mins = total_sec / 60;
            int secs = total_sec % 60;
            
            // \r 대신 std::endl 적용하여 파이썬 버퍼링 체증 해결
            std::cout << "[DAQ] "
                      << std::setfill('0') << std::setw(2) << mins << ":" << std::setw(2) << secs << " | "
                      << "Evt: " << event_count << " | "
                      << std::fixed << std::setprecision(1) << rate << " Hz | "
                      << std::fixed << std::setprecision(1) << speed_mbps << " MB/s | "
                      << "Drop: " << zmq_drops << std::endl;
            
            log_events = 0;
            zmq_drops = 0;
            last_log_time = now;
            
            if (out_stream_.is_open()) {
                out_stream_.flush();
            }
        }

        uint32_t bsize = 0;
        CAEN_DGTZ_ErrorCode err = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, caen_buffer, &bsize);
        
        if (err != CAEN_DGTZ_Success || bsize == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue; 
        }

        uint32_t num_events = 0;
        CAEN_CHECK(CAEN_DGTZ_GetNumEvents(handle, caen_buffer, bsize, &num_events));

        for (uint32_t i = 0; i < num_events; ++i) {
            CAEN_DGTZ_EventInfo_t evt_info;
            char *evt_ptr = nullptr;
            
            CAEN_CHECK(CAEN_DGTZ_GetEventInfo(handle, caen_buffer, bsize, i, &evt_info, &evt_ptr));
            CAEN_CHECK(CAEN_DGTZ_DecodeEvent(handle, evt_ptr, (void **)&caen_event));

            uint32_t current_ttt = evt_info.TriggerTimeTag & TTT_MASK;
            if (current_ttt < prev_ttt) ttt_rollovers++;
            prev_ttt = current_ttt;

            uint32_t actual_trace_size = 0;
            for (int ch = 0; ch < MAX_DT5751_CH; ++ch) {
                if ((evt_info.ChannelMask >> ch) & 1) {
                    actual_trace_size = caen_event->ChSize[ch];
                    break; 
                }
            }

            std::memset(header, 0, sizeof(EventHeader));
            header->ExtendedTTT = (ttt_rollovers << 31) | current_ttt;
            header->EventID = event_count++;
            header->RecordLength = actual_trace_size; 
            header->ChannelMask = evt_info.ChannelMask;
            header->Pattern = evt_info.Pattern;
            header->SampleRate_ps = current_sample_rate_ps_;

            size_t payload_size = sizeof(EventHeader);
            for (int ch = 0; ch < MAX_DT5751_CH; ++ch) {
                if ((header->ChannelMask >> ch) & 1) {
                    uint16_t *wave_src = caen_event->DataChannel[ch];
                    uint32_t trace_size = caen_event->ChSize[ch];
                    if (trace_size == 0) continue;
                    
                    std::memcpy(wave_dest + (payload_size - sizeof(EventHeader)) / sizeof(uint16_t), 
                                wave_src, trace_size * sizeof(uint16_t));
                    payload_size += trace_size * sizeof(uint16_t);
                }
            }

            if (out_stream_.is_open()) {
                out_stream_.write(raw_buffer_pool_.data(), payload_size);
                total_bytes_written += payload_size; 
            }
            
            if (zmq_send(zmq_pub_, raw_buffer_pool_.data(), payload_size, ZMQ_DONTWAIT) < 0) {
                if (zmq_errno() == EAGAIN) zmq_drops++;
            }
            log_events++;
        }
    }

    CAEN_DGTZ_SWStopAcquisition(handle);

    auto end_wall = std::chrono::system_clock::now();
    std::time_t start_t = std::chrono::system_clock::to_time_t(start_wall);
    std::time_t end_t = std::chrono::system_clock::to_time_t(end_wall);
    char start_str[64], end_str[64];
    std::strftime(start_str, sizeof(start_str), "%Y-%m-%d %H:%M:%S", std::localtime(&start_t));
    std::strftime(end_str, sizeof(end_str), "%Y-%m-%d %H:%M:%S", std::localtime(&end_t));

    auto run_duration = std::chrono::duration_cast<std::chrono::seconds>(end_wall - start_wall).count();
    double total_mb = total_bytes_written / (1024.0 * 1024.0);
    double avg_rate = (run_duration > 0) ? static_cast<double>(event_count) / run_duration : 0.0;

    std::cout << "\n\n\033[1;32m========== [ DAQ Run Summary ] ==========\033[0m\n"
              << " - Output File     : " << output_file_ << "\n"
              << " - Start Time      : " << start_str << "\n"
              << " - End Time        : " << end_str << "\n"
              << " - Elapsed Time    : " << run_duration << " seconds\n"
              << " - Total Events    : " << event_count << " events\n"
              << " - Avg Trg Rate    : " << std::fixed << std::setprecision(1) << avg_rate << " Hz\n"
              << " - Total Data Size : " << std::fixed << std::setprecision(2) << total_mb << " MB\n"
              << "\033[1;32m=========================================\033[0m\n" << std::endl;
}