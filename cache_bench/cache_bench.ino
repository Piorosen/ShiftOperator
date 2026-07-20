// RP2040 캐시 구조 검증 벤치마크
// - Cortex-M0+ 에는 L1I/L1D/L2/L3 캐시가 없음
// - 유일한 캐시는 QSPI 플래시 XIP 캐시 (16KB, 2-way, 8-byte line)
// - 이를 실측으로 검증: 워킹셋 크기별 접근 시간 + HW 히트/미스 카운터

#include <mbed.h>

#define XIP_CACHED    0x10000000u  // 캐시 경유 플래시 읽기
#define XIP_NOCACHE   0x13000000u  // 캐시 우회(no-cache no-alloc) 앨리어스
#define XIP_CTRL_BASE 0x14000000u

static volatile uint32_t* const REG_FLUSH   = (volatile uint32_t*)(XIP_CTRL_BASE + 0x04);
static volatile uint32_t* const REG_CTR_HIT = (volatile uint32_t*)(XIP_CTRL_BASE + 0x0c);
static volatile uint32_t* const REG_CTR_ACC = (volatile uint32_t*)(XIP_CTRL_BASE + 0x10);

#define NIDX 4096
static uint32_t idxbuf[NIDX];
static uint32_t rambuf[16384];  // 64 KB SRAM 버퍼

static volatile uint32_t sink;

static uint32_t rng_state = 0x12345678u;
static inline uint32_t xrand() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static void cache_flush() {
  *REG_FLUSH = 1;
  (void)*REG_FLUSH;  // flush 완료까지 읽기가 스톨됨
}

// 무작위 인덱스로 base[]를 반복 읽고 접근당 ns와 XIP 미스율을 출력
static void bench_random(const char* tag, const volatile uint32_t* base,
                         uint32_t window_bytes, int passes, bool use_ctr) {
  mbed::Watchdog::get_instance().kick();
  Serial.print("RUN,"); Serial.print(tag); Serial.print(',');
  Serial.println(window_bytes);
  delay(20);

  uint32_t words = window_bytes / 4;
  for (int i = 0; i < NIDX; i++) idxbuf[i] = xrand() & (words - 1);

  if (use_ctr) cache_flush();  // 캐시와 무관한 SRAM 테스트에선 flush 생략
  uint32_t s = 0;
  for (int i = 0; i < NIDX; i++) s += base[idxbuf[i]];  // 워밍업 1패스

  *REG_CTR_HIT = 0; *REG_CTR_ACC = 0;  // 쓰면 클리어
  uint32_t t0 = micros();
  for (int p = 0; p < passes; p++)
    for (int i = 0; i < NIDX; i++) s += base[idxbuf[i]];
  uint32_t t1 = micros();
  uint32_t hit = *REG_CTR_HIT, acc = *REG_CTR_ACC;
  sink = s;

  uint32_t n = (uint32_t)passes * NIDX;
  float ns = (float)(t1 - t0) * 1000.0f / n;
  float missratio = use_ctr ? (float)(acc - hit) / n : -1.0f;

  Serial.print(tag);           Serial.print(',');
  Serial.print(window_bytes);  Serial.print(',');
  Serial.print(ns, 1);         Serial.print(',');
  Serial.println(missratio, 3);
}

// 순차 접근(stride 지정)으로 캐시 라인 크기 검증: 미스율 = stride/line_size
static void bench_seq(const char* tag, uint32_t stride_bytes,
                      uint32_t window_bytes, int passes) {
  mbed::Watchdog::get_instance().kick();
  const volatile uint32_t* base = (const volatile uint32_t*)XIP_CACHED;
  uint32_t step = stride_bytes / 4;
  uint32_t words = window_bytes / 4;

  cache_flush();
  *REG_CTR_HIT = 0; *REG_CTR_ACC = 0;
  uint32_t s = 0, n = 0;
  uint32_t t0 = micros();
  for (int p = 0; p < passes; p++)
    for (uint32_t i = 0; i < words; i += step) { s += base[i]; n++; }
  uint32_t t1 = micros();
  uint32_t hit = *REG_CTR_HIT, acc = *REG_CTR_ACC;
  sink = s;

  float ns = (float)(t1 - t0) * 1000.0f / n;
  float missratio = (float)(acc - hit) / n;

  Serial.print(tag);           Serial.print(',');
  Serial.print(stride_bytes);  Serial.print(',');
  Serial.print(ns, 1);         Serial.print(',');
  Serial.println(missratio, 3);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  delay(500);
  mbed::Watchdog::get_instance().start(8000);  // 8초 워치독: 멈추면 자동 리부트

  Serial.print("SYSCLK_HZ,");
  Serial.println(SystemCoreClock);

  for (uint32_t i = 0; i < 16384; i++) rambuf[i] = i * 2654435761u;

  // A. 플래시(XIP 캐시 경유) 무작위 접근 — 워킹셋이 16KB를 넘으면 미스 급증 예상
  Serial.println("== A: flash(cached) random, tag,window,ns/access,miss/access ==");
  for (uint32_t kb = 2; kb <= 512; kb *= 2)
    bench_random("FLASH_RND", (const volatile uint32_t*)XIP_CACHED, kb * 1024, 8, true);

  // B. SRAM 무작위 접근 — 캐시가 없으므로 크기와 무관하게 균일 예상
  Serial.println("== B: SRAM random ==");
  for (uint32_t kb = 2; kb <= 64; kb *= 2)
    bench_random("SRAM_RND", (const volatile uint32_t*)rambuf, kb * 1024, 8, false);

  // C. 플래시 캐시 우회 접근 — 순수 QSPI 레이턴시
  Serial.println("== C: flash(uncached) random ==");
  bench_random("FLASH_UNC", (const volatile uint32_t*)XIP_NOCACHE, 64 * 1024, 2, false);

  // D. 순차 접근으로 라인 크기 검증: 8B 라인이면 stride 4B->miss 0.5, 8B->1.0, 16B->1.0
  Serial.println("== D: flash sequential stride, tag,stride,ns/access,miss/access ==");
  bench_seq("FLASH_SEQ", 4, 256 * 1024, 2);
  bench_seq("FLASH_SEQ", 8, 256 * 1024, 2);
  bench_seq("FLASH_SEQ", 16, 256 * 1024, 2);

  Serial.println("DONE");
}

void loop() { delay(1000); }
