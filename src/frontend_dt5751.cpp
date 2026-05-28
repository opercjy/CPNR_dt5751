#include "DAQManager.h"
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <csignal>
#include <atomic>

std::atomic<bool> g_is_running{true};

void sig_handler(int) {
    std::cout << "\n\033[1;33m[Interrupt] Catching Signal. Stopping DAQ Gracefully...\033[0m\n";
    g_is_running = false; 
}

void PrintUsage(const char* prog_name) {
    std::cout << "\n\033[1;36m========================================================\033[0m\n"
              << "\033[1;32m HEP 3-Tier DAQ: DT5751 High-Speed Frontend (CLI Core)\033[0m\n"
              << "\033[1;36m========================================================\033[0m\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -c <file>   : Configuration file path (default: config/dt5751_cli_test.conf)\n"
              << "  -o <file>   : Output raw binary file (.dat) (default: ../data/data_run.dat)\n"
              << "  -n <events> : Max number of events to acquire (0 = infinite)\n"
              << "  -t <sec>    : Max run time in seconds (0 = infinite)\n"
              << "  -h          : Print this help message\n\n"
              << "Example: " << prog_name << " -c bin/config/test.conf -o data/run01.dat -t 60\n"
              << "\033[1;36m========================================================\033[0m\n\n";
}

void PrintConfigContent(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "\033[1;31m[Error] Cannot open config file: " << filepath << "\033[0m\n";
        return;
    }
    std::cout << "\n\033[1;35m=== [ Loaded Configuration : " << filepath << " ] ===\033[0m\n";
    std::string line;
    while (std::getline(file, line)) {
        std::cout << "  " << line << "\n";
    }
    std::cout << "\033[1;35m============================================================\033[0m\n\n";
}

int main(int argc, char** argv) {
    std::string config_file = "config/dt5751_cli_test.conf";
    std::string output_file = "../data/data_run.dat";
    int max_events = 0;       
    int run_time_sec = 0;     

    int opt;
    while ((opt = getopt(argc, argv, "c:o:n:t:h")) != -1) {
        switch (opt) {
            case 'c': config_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 'n': max_events = std::stoi(optarg); break;
            case 't': run_time_sec = std::stoi(optarg); break;
            case 'h': PrintUsage(argv[0]); return 0;
            default: PrintUsage(argv[0]); return 1;
        }
    }

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    try {
        PrintConfigContent(config_file);
        std::cout << "\033[1;32m[Frontend] System Boot. Output Target : \033[0m" << output_file << "\n";
        DAQManager daq(config_file, output_file, max_events, run_time_sec);
        daq.Start(g_is_running);
    } catch (const std::exception& e) {
        std::cerr << "\n\033[1;31m[Fatal Error]\033[0m " << e.what() << "\n";
        return 1;
    }
    return 0;
}