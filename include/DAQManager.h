#ifndef DAQ_MANAGER_H
#define DAQ_MANAGER_H

#include "CaenDigitizer.h"
#include "ConfigParser.h"
#include "EventHeader.h"
#include <atomic>
#include <fstream>
#include <string>
#include <vector>

class DAQManager {
public:
  DAQManager(const std::string &config_file, const std::string &output_file,
             int max_events, int run_time_sec);
  ~DAQManager();
  
  void Start(std::atomic<bool>& is_running);
  void Stop();

private:
  void SetupHardware();
  void AcquisitionLoop(std::atomic<bool>& is_running);

  ConfigParser config_;
  std::string output_file_;
  int max_events_;
  int run_time_sec_;
  std::atomic<bool> running_;
  CaenDigitizer digitizer_;
  std::ofstream out_stream_;
  
  // ZMQ 및 데이터 속성
  void *zmq_ctx_;
  void *zmq_pub_;
  uint16_t current_sample_rate_ps_; // 현재 동작 중인 샘플링 속도 (1000 or 500)
  
  std::vector<char> raw_buffer_pool_;
};

#endif // DAQ_MANAGER_H