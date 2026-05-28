#include "EventHeader.h"
#include <TApplication.h>
#include <TCanvas.h>
#include <TFile.h>
#include <TGraph.h>
#include <TAxis.h>
#include <TTree.h>
#include <TMacro.h>
#include <TParameter.h>
#include <TSystem.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <csignal>
#include <numeric>
#include <sys/select.h>
#include <unistd.h>
#include <map> // 🌟 Time Travel을 위한 헤더 추가

#ifdef __ROOTCLING__
#pragma link C++ class std::vector<uint16_t>+;
#endif

volatile std::sig_atomic_t g_running = 1;

void sig_handler(int) {
    std::cout << "\n\033[1;33m[Interrupt] Received stop signal. Saving ROOT file gracefully...\033[0m\n";
    g_running = 0;
}

void PrintUsage(const char* prog_name) {
    std::cout << "\n\033[1;36m========================================================\033[0m\n"
              << "\033[1;32m HEP 3-Tier DAQ: DT5751 Offline Production (ROOT)\033[0m\n"
              << "\033[1;36m========================================================\033[0m\n"
              << "Usage: " << prog_name << " -i <input.dat> [options]\n\n"
              << "Options:\n"
              << "  -i <file>   : Input raw binary file (.dat) (Required)\n"
              << "  -o <file>   : Output ROOT file (.root) (Auto-generated if omitted)\n"
              << "  -c <file>   : Config file to embed in ROOT TMacro (Optional)\n"
              << "  -r <num>    : Run number to tag in ROOT file (default: 0)\n"
              << "  -d <id>     : Interactive Debugger mode for specific Event ID\n"
              << "  -w          : Save full waveforms to ROOT file (Heavy)\n"
              << "  -h          : Print this help message\n\n"
              << "Example: " << prog_name << " -i data/test_run.dat -d 0\n"
              << "\033[1;36m========================================================\033[0m\n\n";
}

int main(int argc, char **argv) {
    std::string input_file = "";
    std::string output_file = "";
    std::string config_file = ""; 
    int debug_event_id = -1;
    int run_number = 0;
    bool save_waveform = false; 

    int opt;
    while ((opt = getopt(argc, argv, "i:o:c:r:d:wh")) != -1) {
        switch (opt) {
            case 'i': input_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 'c': config_file = optarg; break;
            case 'r': run_number = std::stoi(optarg); break;
            case 'd': debug_event_id = std::stoi(optarg); break;
            case 'w': save_waveform = true; break;
            case 'h': PrintUsage(argv[0]); return 0;
            default: PrintUsage(argv[0]); return 1;
        }
    }

    if (input_file.empty() && optind < argc) input_file = argv[optind];
    if (input_file.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    if (output_file.empty() && debug_event_id < 0) {
        size_t last_dot = input_file.find_last_of(".");
        size_t last_slash = input_file.find_last_of("/\\");
        if (last_dot == std::string::npos || (last_slash != std::string::npos && last_dot < last_slash)) {
            output_file = input_file + "_prod.root";
        } else {
            output_file = input_file.substr(0, last_dot) + "_prod.root";
        }
    }

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    std::ifstream ifs;
    std::vector<char> read_buffer(4 * 1024 * 1024);
    ifs.rdbuf()->pubsetbuf(read_buffer.data(), read_buffer.size());
    
    ifs.open(input_file, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "\033[1;31m[Error]\033[0m Cannot open input file: " << input_file << "\n";
        return 1;
    }

    ifs.seekg(0, std::ios::end);
    size_t total_bytes = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    size_t processed_bytes = 0;

    TApplication *app = nullptr;
    TCanvas *c1 = nullptr;
    if (debug_event_id >= 0) {
        app = new TApplication("App", &argc, argv);
        c1 = new TCanvas("c1", "Interactive Debugger (DT5751)", 1000, 600);
    }

    TFile *fOut = nullptr;
    TTree *tOut = nullptr;
    EventHeader header;
    
    uint32_t record_len_branch = 0; 
    uint16_t sample_rate_branch = 0;
    
    std::vector<uint16_t> wave_ch[MAX_DT5751_CH];
    double charge_ch[MAX_DT5751_CH] = {0.0};
    double pulse_height_ch[MAX_DT5751_CH] = {0.0};
    double pulse_start_time_ch[MAX_DT5751_CH] = {0.0}; 
    double baseline_ch[MAX_DT5751_CH] = {0.0}; 

    if (debug_event_id < 0) {
        fOut = new TFile(output_file.c_str(), "RECREATE");
        if (!config_file.empty()) {
            std::ifstream cfs(config_file);
            if (cfs.is_open()) {
                TMacro config_macro(config_file.c_str());
                config_macro.Write("RunConfig");
            }
        }
        TParameter<int> p_run_num("RunNumber", run_number);
        p_run_num.Write();

        tOut = new TTree("phys_tree", "DT5751 Physics Data");
        tOut->Branch("EventID", &header.EventID, "EventID/i");
        tOut->Branch("SyncTime_TTT", &header.ExtendedTTT, "SyncTime_TTT/l");
        tOut->Branch("ChannelMask", &header.ChannelMask, "ChannelMask/s");
        tOut->Branch("RecordLength", &record_len_branch, "RecordLength/i"); 
        tOut->Branch("SampleRate_ps", &sample_rate_branch, "SampleRate_ps/s");

        for (int i = 0; i < MAX_DT5751_CH; ++i) {
            tOut->Branch(Form("Charge_CH%d", i), &charge_ch[i], Form("Charge_CH%d/D", i));
            tOut->Branch(Form("PulseHeight_CH%d", i), &pulse_height_ch[i], Form("PulseHeight_CH%d/D", i));
            tOut->Branch(Form("PulseStart_T0_CH%d", i), &pulse_start_time_ch[i], Form("PulseStart_T0_CH%d/D", i));
            tOut->Branch(Form("Baseline_CH%d", i), &baseline_ch[i], Form("Baseline_CH%d/D", i)); 
            if (save_waveform) tOut->Branch(Form("Waveform_CH%d", i), &wave_ch[i]);
        }
    }

    std::vector<uint16_t> raw_waveform_buffer;
    uint32_t current_event = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    // 🌟 [추가] 파일 포인터 위치를 기억하는 타임머신 맵
    std::map<uint32_t, std::streampos> evt_pos_map;

    std::cout << "\033[1;32m[Production] Starting Universal Conversion (DT5751)...\033[0m\n";

    while (g_running) {
        std::streampos current_pos = ifs.tellg();
        if (!ifs.read(reinterpret_cast<char *>(&header), sizeof(EventHeader))) break;

        // 🌟 처음 만난 이벤트인지 확인하여 중복 기록 방지
        bool is_new_event = (evt_pos_map.find(header.EventID) == evt_pos_map.end());
        if (is_new_event) {
            evt_pos_map[header.EventID] = current_pos;
            processed_bytes += sizeof(EventHeader);
            current_event++;
        }

        record_len_branch = header.RecordLength; 
        sample_rate_branch = header.SampleRate_ps;
        double dt_ns = header.SampleRate_ps / 1000.0; 

        int active_ch = 0;
        for (int i = 0; i < MAX_DT5751_CH; ++i) {
            if ((header.ChannelMask >> i) & 1) active_ch++;
            wave_ch[i].clear();
            charge_ch[i] = 0.0;
            pulse_height_ch[i] = 0.0;
            pulse_start_time_ch[i] = -1.0;
            baseline_ch[i] = 0.0;
        }

        size_t wave_len = header.RecordLength * active_ch;
        size_t wave_bytes_size = wave_len * sizeof(uint16_t);

        raw_waveform_buffer.resize(wave_len);
        ifs.read(reinterpret_cast<char *>(raw_waveform_buffer.data()), wave_bytes_size);
        
        if (is_new_event) {
            processed_bytes += wave_bytes_size;
        }

        int offset = 0;
        for (int ch = 0; ch < MAX_DT5751_CH; ++ch) {
            if ((header.ChannelMask >> ch) & 1) {
                uint16_t* trace_ptr = raw_waveform_buffer.data() + offset;
                size_t trace_len = header.RecordLength;

                if (trace_len > 0) {
                    size_t baseline_samples = std::min((size_t)150, (size_t)(trace_len * 0.25));
                    double baseline = 0.0;
                    for(size_t i = 0; i < baseline_samples; ++i) baseline += trace_ptr[i];
                    baseline /= baseline_samples;
                    baseline_ch[ch] = baseline;

                    double charge = 0.0;
                    double min_val = 1024.0; 
                    double trigger_threshold = baseline - 5.0; 

                    for(size_t i = baseline_samples; i < trace_len; ++i) {
                        if (trace_ptr[i] < baseline) charge += (baseline - trace_ptr[i]);
                        if (trace_ptr[i] < min_val) min_val = trace_ptr[i];
                        if (pulse_start_time_ch[ch] < 0 && trace_ptr[i] < trigger_threshold) {
                            pulse_start_time_ch[ch] = i * dt_ns; 
                        }
                    }
                    
                    charge_ch[ch] = (charge > 0) ? charge : 0.0;
                    pulse_height_ch[ch] = (min_val < baseline) ? (baseline - min_val) : 0.0;
                }

                if (save_waveform || (debug_event_id >= 0 && (int)header.EventID == debug_event_id)) {
                    wave_ch[ch].assign(trace_ptr, trace_ptr + trace_len);
                }
                offset += trace_len;
            }
        }

        // 🌟 중복 필터: 오직 '새로운 이벤트'일 때만 ROOT 파일에 기록
        if (tOut && is_new_event) tOut->Fill();

        if (current_event % 2000 == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time).count();
            double progress = (static_cast<double>(processed_bytes) / total_bytes) * 100.0;
            double speed_bps = processed_bytes / elapsed_sec; 
            double eta_sec = (total_bytes - processed_bytes) / speed_bps;

            std::cout << "\r\033[K" << "\033[1;36m[Progress]\033[0m " 
                      << std::fixed << std::setprecision(1) << progress << "% | "
                      << "Events: " << current_event << " | "
                      << "Speed: " << std::setprecision(1) << (speed_bps / 1024.0 / 1024.0) << " MB/s | "
                      << "ETA: " << (int)eta_sec << " s" << std::flush;
        }

        // ==============================================================================
        // 🌟 타임머신이 장착된 인터랙티브 디버거 모드
        // ==============================================================================
        if (debug_event_id >= 0 && (int)header.EventID == debug_event_id && active_ch > 0) {
            int disp_ch = 0;
            for (; disp_ch < MAX_DT5751_CH; ++disp_ch) {
                if ((header.ChannelMask >> disp_ch) & 1) break;
            }
            
            std::vector<double> x(header.RecordLength), y(header.RecordLength);
            for (size_t i = 0; i < header.RecordLength; ++i) {
                x[i] = i * dt_ns; 
                y[i] = baseline_ch[disp_ch] - wave_ch[disp_ch][i]; 
            }
            
            TGraph *gr = new TGraph(header.RecordLength, x.data(), y.data());
            gr->SetTitle(Form("Event %d (CH%d) - Charge: %.1f, PHA: %.1f, T0: %.1f ns;Time (ns);Inverted ADC Amplitude", 
                              debug_event_id, disp_ch, charge_ch[disp_ch], pulse_height_ch[disp_ch], pulse_start_time_ch[disp_ch]));
            gr->SetLineColor(kBlue);
            gr->SetLineWidth(2);
            gr->GetYaxis()->SetRangeUser(-20, pulse_height_ch[disp_ch] * 1.5 + 50); 
            gr->Draw("AL");

            TGraph* bl_line = new TGraph(2);
            bl_line->SetPoint(0, 0, 0.0);
            bl_line->SetPoint(1, header.RecordLength * dt_ns, 0.0);
            bl_line->SetLineColor(kRed);
            bl_line->SetLineStyle(2);
            bl_line->SetLineWidth(2);
            bl_line->Draw("L SAME");

            c1->Update();
            
            std::cout << "\n\n\033[1;33m[Debugger] Displaying Event " << debug_event_id << " CH" << disp_ch << " (Inverted Waveform)\033[0m\n";
            std::cout << "RecordLength: " << header.RecordLength << " | dt: " << dt_ns << " ns/Sample | Raw Baseline: " << baseline_ch[disp_ch] << "\n";
            std::cout << "[WAITING_CMD] Ready for Terminal Input (p: prev / n: next / j <id>: jump / q: quit)...\n";
            std::cout << std::flush; 

            std::string cmd;
            bool continue_debug = true;
            
            while (continue_debug && g_running) {
                gSystem->ProcessEvents(); 

                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);
                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000; 

                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                    std::cin >> cmd;
                    if (cmd == "q" || cmd == "quit") {
                        std::cout << "\n[Debugger] Exiting debugger. Resuming full conversion...\n";
                        debug_event_id = -1; 
                        continue_debug = false;
                        if(c1) { c1->Close(); delete c1; c1 = nullptr; }
                    } 
                    else if (cmd == "n" || cmd == "next") {
                        debug_event_id++; 
                        continue_debug = false;
                    } 
                    // 🌟 [수정] 이전 탐색 기능 활성화
                    else if (cmd == "p" || cmd == "prev") {
                        int target = debug_event_id - 1;
                        if (evt_pos_map.count(target)) {
                            debug_event_id = target;
                            ifs.clear(); // EOF 상태 등 리셋
                            ifs.seekg(evt_pos_map[target]);
                            continue_debug = false;
                        } else {
                            std::cout << "\n[Debugger] Event " << target << " is not in history (Memory).\n";
                        }
                    } 
                    // 🌟 [수정] 자유 점프 기능 활성화
                    else if (cmd == "j" || cmd == "jump") {
                        int target;
                        std::cin >> target;
                        if (evt_pos_map.count(target)) {
                            // 과거로의 점프 (메모리에 있음)
                            debug_event_id = target;
                            ifs.clear();
                            ifs.seekg(evt_pos_map[target]);
                            continue_debug = false;
                        } else if (target > debug_event_id) {
                            // 미래로의 점프 (계속 읽으면서 찾아감)
                            debug_event_id = target;
                            continue_debug = false;
                        } else {
                            std::cout << "\n[Debugger] Target event is not in history.\n";
                        }
                    }
                    std::cout << std::flush;
                }
            }
            if (debug_event_id >= 0) continue; 
        }
    }

    if (g_running && debug_event_id < 0) {
        std::cout << "\r\033[K\033[1;32m[Progress]\033[0m 100.0% | Events: " << current_event << " | Done.\n";
    }

    if (fOut) {
        fOut->Write();
        fOut->Close();
        delete fOut;
    }

    // 🌟 오프라인 처리 종료 시 요약 보고서 출력
    auto end_time = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count();
    double total_mb = processed_bytes / (1024.0 * 1024.0);
    double avg_speed = (total_sec > 0) ? (total_mb / total_sec) : 0.0;

    if (debug_event_id < 0) {
        std::cout << "\n\033[1;35m========== [ Conversion Summary ] ==========\033[0m\n"
                  << " - Source File     : " << input_file << "\n"
                  << " - Output File     : " << output_file << "\n"
                  << " - Elapsed Time    : " << std::fixed << std::setprecision(2) << total_sec << " seconds\n"
                  << " - Processed Evts  : " << current_event << " events\n"
                  << " - Processed Size  : " << std::fixed << std::setprecision(2) << total_mb << " MB\n"
                  << " - Avg Speed       : " << std::fixed << std::setprecision(2) << avg_speed << " MB/s\n"
                  << "\033[1;35m============================================\033[0m\n\n";
    }

    if (app) delete app;
    return 0;
}