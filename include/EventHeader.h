#ifndef EVENT_HEADER_H
#define EVENT_HEADER_H
#include <cstdint>

constexpr int MAX_DT5751_CH = 4;

#pragma pack(push, 1)
struct EventHeader {
  uint64_t ExtendedTTT;     // 8 Bytes: TTT Rollover 보정된 트리거 시간
  uint32_t EventID;         // 4 Bytes: 보드 내부 이벤트 카운터
  uint32_t RecordLength;    // 4 Bytes: 채널당 샘플 길이
  uint16_t ChannelMask;     // 2 Bytes: 활성 채널 마스크
  uint16_t Pattern;         // 2 Bytes: 트리거 패턴
  uint16_t SampleRate_ps;   // 2 Bytes: 1 샘플당 물리 시간 (1000 = 1ns, 500 = 0.5ns)
  uint16_t Reserved;        // 2 Bytes: 24 Bytes 패딩 맞춤용 예비
};
#pragma pack(pop)

#endif // EVENT_HEADER_H