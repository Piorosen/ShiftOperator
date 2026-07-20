// 요구사항: 데이터 50,000개, value=32 고정. LUT 금지.
//   s>0 → 32 << s (왼쪽), s<0 → 32 >> -s (오른쪽).
//   절반 양수(1..20), 절반 음수(-1..-20), 값 제한 없음.
//
//  v1 origin  : 분기 + 시프트 (기준)
//  v2 pow2    : 분기 제거 — 32=2^5 → e=s+5로 통일해 1<<e. e<0(즉 s<-5, 결과 0)은
//               부호 마스크로 0 처리. 테이블 없이 순수 연산.
//  v3 pow2_w  : v2 + 지시자 4개를 워드 1회 로드로 배칭
//  v4 jit     : 청크 스트림 JIT. 부호는 명령어의 비트11 하나만 결정
//               (LSLS 0x0000 / LSRS 0x0800) — 원본 SHIFT 매크로처럼 부호 마스크로
//               분기 없이 opcode 생성. 시프트 양은 imm5에 그대로.
//               원소당 3명령어: movs r3,#32 / lsls|lsrs r3,#n / adds r2,r3

#pragma GCC optimize("O3")

#if __has_include("platform/mbed_mpu_mgmt.h")
#include "platform/mbed_mpu_mgmt.h"
#endif

#define N      50000
#define PASSES 20
#define VALUE  32u
#define CHUNK  8192

static int8_t sh[N];                               // 50KB
alignas(4) static uint16_t cstream[3 * CHUNK + 4]; // 49KB JIT 청크 버퍼
typedef unsigned int (*jitfnV)();
static volatile uint32_t sink;

#define SGN(X)   ((int32_t)(X) >> 31)
#define ABS(X)   ((uint32_t)((((int32_t)(X)) + SGN(X)) ^ SGN(X)))

static uint32_t rng_state = 0x12345678u;
static inline uint32_t xrand() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

#define BENCH(name, BODY)                                        \
    do {                                                         \
        uint32_t acc = 0;                                        \
        uint32_t t0 = micros();                                  \
        for (int p = 0; p < PASSES; p++) { BODY; }               \
        uint32_t t1 = micros();                                  \
        sink = acc;                                              \
        uint32_t total = (uint32_t)PASSES * N;                   \
        Serial.print(name); Serial.print(",");                   \
        Serial.print((t1 - t0) / 1000); Serial.print("ms,");     \
        Serial.print((float)(t1 - t0) * 1000.0f / total, 1);     \
        Serial.print("ns/op,sum=");                              \
        Serial.println(acc);                                     \
    } while (0)

// 분기 없는 단일 원소 계산: e=s+5, 1<<(e&31)을 만들되 e<0이면 마스크로 0
static inline uint32_t shift_pow2(int8_t s) {
    int32_t e = (int32_t)s + 5;
    return (1u << ((uint32_t)e & 31)) & ~(uint32_t)(e >> 31);
}

// 분기·마스크 없는 element-wise: ARM 레지스터 시프트는 양이 32 이상이면
// 결과가 0이 되는 성질을 이용. n=(uint8)s 하나로 두 방향을 동시에 계산하면
// 부호에 따라 한쪽이 하드웨어에 의해 자동으로 0이 됨.
//   양수 s: 32<<s 유효, 32>>(-s&0xFF)=32>>(236..255)→0
//   음수 s: 32<<(236..255)→0, 32>>|s| 유효
static inline uint32_t shift_dual(uint32_t n /* (uint8_t)s */) {
    uint32_t a = VALUE, b = VALUE, n2;
    __asm volatile(
        ".syntax unified \n\t"
        "negs %2, %3     \n\t"   // n2 = -n (하위 8비트만 유효하면 됨)
        "lsls %0, %0, %3 \n\t"   // a = 32 << n   (n>=32 → 0)
        "lsrs %1, %1, %2 \n\t"   // b = 32 >> n2  (n2>=32 → 0)
        ".syntax divided \n\t"
        : "+l"(a), "+l"(b), "=&l"(n2)
        : "l"(n)
        : "cc");
    return a + b;   // 항상 한쪽은 0
}

// dual 아이디어를 손으로 짠 어셈블리 루프로: 원소당 9명령어, 4배 언롤로
// 루프 오버헤드 분산, 레지스터 할당을 직접 제어해 스필 제거. n은 4의 배수.
static uint32_t __attribute__((noinline)) sum_dual_asm(const uint8_t* p, int n) {
    uint32_t acc = 0;
    const uint8_t* end = p + n;
    __asm volatile(
        ".syntax unified        \n"
        "1:                     \n\t"
        "ldrb r3, [%0, #0]      \n\t"
        "movs r4, #32           \n\t"
        "lsls r4, r4, r3        \n\t"
        "negs r3, r3            \n\t"
        "movs r5, #32           \n\t"
        "lsrs r5, r5, r3        \n\t"
        "adds %1, %1, r4        \n\t"
        "adds %1, %1, r5        \n\t"
        "ldrb r3, [%0, #1]      \n\t"
        "movs r4, #32           \n\t"
        "lsls r4, r4, r3        \n\t"
        "negs r3, r3            \n\t"
        "movs r5, #32           \n\t"
        "lsrs r5, r5, r3        \n\t"
        "adds %1, %1, r4        \n\t"
        "adds %1, %1, r5        \n\t"
        "ldrb r3, [%0, #2]      \n\t"
        "movs r4, #32           \n\t"
        "lsls r4, r4, r3        \n\t"
        "negs r3, r3            \n\t"
        "movs r5, #32           \n\t"
        "lsrs r5, r5, r3        \n\t"
        "adds %1, %1, r4        \n\t"
        "adds %1, %1, r5        \n\t"
        "ldrb r3, [%0, #3]      \n\t"
        "movs r4, #32           \n\t"
        "lsls r4, r4, r3        \n\t"
        "negs r3, r3            \n\t"
        "movs r5, #32           \n\t"
        "lsrs r5, r5, r3        \n\t"
        "adds %1, %1, r4        \n\t"
        "adds %1, %1, r5        \n\t"
        "adds %0, %0, #4        \n\t"
        "cmp %0, %2             \n\t"
        "bne 1b                 \n\t"
        ".syntax divided        \n"
        : "+l"(p), "+l"(acc)
        : "l"(end)
        : "r3", "r4", "r5", "cc", "memory");
    return acc;
}


void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    delay(500);

#if __has_include("platform/mbed_mpu_mgmt.h")
    mbed_mpu_manager_lock_ram_execution();
#endif

    // 절반 양수(1..20), 절반 음수(-1..-20)
    for (int i = 0; i < N; i++) {
        int8_t n = (int8_t)(xrand() % 20 + 1);
        sh[i] = (xrand() & 1) ? n : (int8_t)-n;
    }

    // 정합성 기준: origin 1패스 합
    uint32_t expected = 0;
    for (int i = 0; i < N; i++) {
        int8_t s = sh[i];
        expected += (s > 0) ? (VALUE << s) : (VALUE >> (uint8_t)-s);
    }

    // pow2 검증
    {
        uint32_t got = 0;
        for (int i = 0; i < N; i++) got += shift_pow2(sh[i]);
        Serial.print("VERIFY,pow2,");
        Serial.println(got == expected ? "ok" : "MISMATCH");
    }
    // dual 검증
    {
        uint32_t got = 0;
        for (int i = 0; i < N; i++) got += shift_dual((uint8_t)sh[i]);
        Serial.print("VERIFY,dual,");
        Serial.println(got == expected ? "ok" : "MISMATCH");
    }
    // dual_asm 검증
    {
        uint32_t got = sum_dual_asm((const uint8_t*)sh, N);
        Serial.print("VERIFY,dual_asm,");
        Serial.println(got == expected ? "ok" : "MISMATCH");
    }

    // v1: 분기 + 시프트
    BENCH("origin", {
        const int8_t* ps = sh;
        for (int i = 0; i < N; i++) {
            int8_t s = ps[i];
            acc += (s > 0) ? (VALUE << s) : (VALUE >> (uint8_t)-s);
        }
    });

    // v2: 분기 제거
    BENCH("pow2", {
        const int8_t* ps = sh;
        for (int i = 0; i < N; i++)
            acc += shift_pow2(ps[i]);
    });

    // v3: v2 + 워드 배칭
    BENCH("pow2_w", {
        const uint32_t* pw = (const uint32_t*)sh;
        for (int i = 0; i < N / 4; i++) {
            uint32_t w = pw[i];
            acc += shift_pow2((int8_t)(w));
            acc += shift_pow2((int8_t)(w >> 8));
            acc += shift_pow2((int8_t)(w >> 16));
            acc += shift_pow2((int8_t)(w >> 24));
        }
    });

    // v3.5: 하드웨어 시프트 포화(>=32 → 0) 이용, 분기·마스크 제거
    BENCH("dual", {
        const uint8_t* ps = (const uint8_t*)sh;
        for (int i = 0; i < N; i++)
            acc += shift_dual(ps[i]);
    });

    // v3.6: dual + 워드 배칭
    BENCH("dual_w", {
        const uint32_t* pw = (const uint32_t*)sh;
        for (int i = 0; i < N / 4; i++) {
            uint32_t w = pw[i];
            acc += shift_dual(w & 0xFF);
            acc += shift_dual((w >> 8) & 0xFF);
            acc += shift_dual((w >> 16) & 0xFF);
            acc += shift_dual(w >> 24);
        }
    });

    // v3.7: 손으로 짠 asm 루프 (element-wise branchless의 바닥 확인)
    BENCH("dual_asm", {
        acc += sum_dual_asm((const uint8_t*)sh, N);
    });

    Serial.println("DONE");
}

void loop() { delay(1000); }
