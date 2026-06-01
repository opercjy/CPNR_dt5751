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
#include <string>

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
    
    int linger = 1000;
    zmq_setsockopt(zmq_pub_, ZMQ_LINGER, &linger, sizeof(linger));
    
    zmq_bind(zmq_pub_, "tcp://127.0.0.1:5555");

    if (!output_file_.empty()) {
        std::filesystem::path p(output_file_);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        
        out_stream_.open(output_file_, std::ios::out | std::ios::binary);
        if (!out_stream_.is_open()) {
            std::cerr << "\n[Fatal Error] Cannot open output file: " << output_file_ << "\n";
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
    std::cout << "[DAQManager] Configuring DT5751 Hardware from Config...\n";
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
    
    uint32_t master_offset = config_.GetInt("Channel_0", "DCOffset", 32768);
    uint32_t master_thr    = config_.GetInt("Channel_0", "TriggerThreshold", 500);

    for (int ch = 0; ch < MAX_DT5751_CH; ++ch) {
        if ((channel_mask >> ch) & 1) {
            std::string ch_sec = "Channel_" + std::to_string(ch);
            
            uint32_t offset = config_.GetInt(ch_sec, "DCOffset", master_offset); 
            uint32_t thr = config_.GetInt(ch_sec, "TriggerThreshold", master_thr);
            
            if (thr > 1023) {
                std::cerr << "[Warning] CH" << ch << " Threshold (" << thr << ") exceeds 10-bit limit. Forced to 1023.\n";
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

    int always_triggered = config_.GetInt("Digitizer", "AlwaysTriggered", 0);
    const uint32_t NEVENTS_LEFT = 10;
    bool auto_stopped = false;

    std::cout << "\n[DAQ Started] Press Ctrl+C to stop gracefully." << std::endl;

    while (is_running) {
        auto now = std::chrono::steady_clock::now();

        if (max_events_ > 0 && (int)event_count >= max_events_) {
            std::cout << "\n[System] Event Limit Reached. Stopping..." << std::endl;
            auto_stopped = true; 
            break;
        }
        if (run_time_sec_ > 0) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= run_time_sec_) {
                std::cout << "\n[System] Time Limit Reached. Stopping..." << std::endl;
                auto_stopped = true;
                break;
            }
        }

        double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time).count();
        if (elapsed_ms >= 1000.0) {
            double rate = (log_events / elapsed_ms) * 1000.0;
            double speed_mbps = ((total_bytes_written - last_bytes_written) / 1048576.0) / (elapsed_ms / 1000.0);
            last_bytes_written = total_bytes_written;
            
            auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            int mins = total_sec / 60;
            int secs = total_sec % 60;

            // 하드웨어 온도 폴링 처리 (1초 간격)
            uint32_t board_temp = 0;
            CAEN_DGTZ_ReadTemperature(handle, 0, &board_temp);
            
            std::cout << "[DAQ] "
                      << std::setfill('0') << std::setw(2) << mins << ":" << std::setw(2) << secs << " | "
                      << "Evt: " << event_count << " | "
                      << std::fixed << std::setprecision(1) << rate << " Hz | "
                      << std::fixed << std::setprecision(1) << speed_mbps << " MB/s | "
                      << "Drop: " << zmq_drops << " | "
                      << "Temp: " << board_temp << " C" << std::endl;
            
            log_events = 0;
            zmq_drops = 0;
            last_log_time = now;
            
            if (out_stream_.is_open()) {
                out_stream_.flush();
            }

            // STAT 멀티파트 메시지로 텔레메트리 데이터 전송
            std::string stat_payload = "{\"temp\":" + std::to_string(board_temp) + "}";
            zmq_send(zmq_pub_, "STAT", 4, ZMQ_SNDMORE);
            zmq_send(zmq_pub_, stat_payload.c_str(), stat_payload.size(), ZMQ_DONTWAIT);
        }

        if (always_triggered > 0) {
            CAEN_DGTZ_SendSWtrigger(handle);
        }

        uint32_t num_events_in_ram = 0;
        CAEN_DGTZ_ReadRegister(handle, 0x812C, &num_events_in_ram);

        if (num_events_in_ram > NEVENTS_LEFT) {
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
                
                zmq_send(zmq_pub_, "DATA", 4, ZMQ_SNDMORE);
                if (zmq_send(zmq_pub_, raw_buffer_pool_.data(), payload_size, ZMQ_DONTWAIT) < 0) {
                    if (zmq_errno() == EAGAIN) zmq_drops++;
                }
                log_events++;
            }
        } else {
            if (always_triggered == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
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

    std::cout << "\n\n========== [ DAQ Run Summary ] ==========\n"
              << " - Output File     : " << output_file_ << "\n"
              << " - Start Time      : " << start_str << "\n"
              << " - End Time        : " << end_str << "\n"
              << " - Elapsed Time    : " << run_duration << " seconds\n"
              << " - Total Events    : " << event_count << " events\n"
              << " - Avg Trg Rate    : " << std::fixed << std::setprecision(1) << avg_rate << " Hz\n"
              << " - Total Data Size : " << std::fixed << std::setprecision(2) << total_mb << " MB\n"
              << "=========================================\n" << std::endl;

    if (auto_stopped) {
        zmq_send(zmq_pub_, "CTRL", 4, ZMQ_SNDMORE);
        zmq_send(zmq_pub_, "RUN_COMPLETED", 13, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
    }
}
