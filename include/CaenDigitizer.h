#ifndef CAEN_DIGITIZER_H
#define CAEN_DIGITIZER_H

#include <CAENDigitizer.h>
#include <iostream>
#include <stdexcept>
#include <string>

// CAEN API 에러 검증 매크로 (에러 발생 시 즉각적인 예외 처리 및 하드웨어 추적)
#define CAEN_CHECK(call) \
    do { \
        CAEN_DGTZ_ErrorCode err = (call); \
        if (err != CAEN_DGTZ_Success) { \
            throw std::runtime_error(std::string("CAEN API Error Code: ") + std::to_string(err)); \
        } \
    } while(0)

class CaenDigitizer {
public:
  CaenDigitizer(CAEN_DGTZ_ConnectionType linkType, int linkNum, int conetNode, uint32_t vmeBaseAddress)
      : handle_(-1), caen_buffer_(nullptr), caen_event_(nullptr) {
      uint32_t link_arg = static_cast<uint32_t>(linkNum);
      // USB를 통한 하드웨어 개통
      CAEN_CHECK(CAEN_DGTZ_OpenDigitizer2(linkType, &link_arg, conetNode, vmeBaseAddress, &handle_));
      // 이전 찌꺼기를 날리기 위한 보드 소프트 리셋
      CAEN_CHECK(CAEN_DGTZ_Reset(handle_));
  }

  ~CaenDigitizer() {
      if (handle_ >= 0) {
          CAEN_DGTZ_SWStopAcquisition(handle_);
          // 메모리 누수 방지 (Graceful Cleanup)
          if (caen_buffer_) CAEN_DGTZ_FreeReadoutBuffer(&caen_buffer_);
          if (caen_event_) CAEN_DGTZ_FreeEvent(handle_, (void **)&caen_event_);
          CAEN_DGTZ_CloseDigitizer(handle_);
      }
  }

  void AllocateBuffers() {
      uint32_t size = 0;
      // 데이터 획득용 이진 버퍼 할당
      CAEN_CHECK(CAEN_DGTZ_MallocReadoutBuffer(handle_, &caen_buffer_, &size));
      // DT5751 10-bit 데이터를 담아낼 구조체 (메모리 구조상 16-bit 래퍼 재활용)
      CAEN_CHECK(CAEN_DGTZ_AllocateEvent(handle_, (void **)&caen_event_));
  }

  int GetHandle() const { return handle_; }
  char *GetReadoutBuffer() const { return caen_buffer_; }
  CAEN_DGTZ_UINT16_EVENT_t *GetDecodedEvent() const { return caen_event_; }

  void WriteRegister(uint32_t reg, uint32_t value) {
      CAEN_CHECK(CAEN_DGTZ_WriteRegister(handle_, reg, value));
  }
  
  uint32_t ReadRegister(uint32_t reg) const {
      uint32_t value = 0;
      CAEN_CHECK(CAEN_DGTZ_ReadRegister(handle_, reg, &value));
      return value;
  }

private:
  int handle_;
  char *caen_buffer_;
  CAEN_DGTZ_UINT16_EVENT_t *caen_event_;
};

#endif // CAEN_DIGITIZER_H