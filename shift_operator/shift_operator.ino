// ShiftOperator RP2040 포팅 + 반복 최적화 (3차)
//
// 변형별 전략:
//  v1 origin     : if 분기 + 시프트 (원본 C 구현)
//  v2 memory     : 원본 포팅 — value 리터럴 + 명령어를 매번 패치, 무인자 JIT 호출
//  v3 memory2    : value는 r0 인자로 전달(패치 제거), 명령어는 41개 LUT에서 로드해 패치
//  v4 table      : 41가지 시프트 함수를 부팅 시 전부 생성해두고 테이블 디스패치 (런타임 패치 0회)
//  v5 branchless : 순수 C 분기 제거 — M0+엔 분기 예측기가 없어 오히려 손해임을 확인용으로 유지
//
// 3차 변경:
//  - 파일 전체 O3 (코어 기본 -Os 대비 루프 오버헤드 축소)
//  - asm memory 배리어 제거: 전역 배열 주소가 함수 포인터로 탈출했으므로
//    불투명한 간접 호출 앞에서 store가 유지됨 (정합성 검증으로 확인)
//  - 핫루프에서 전역(func2/ilut/ftab/code2)을 지역으로 호이스팅 — 매 반복
//    재로드 제거 (불투명 호출이 전역을 건드릴 수 있다고 가정되기 때문)

#pragma GCC optimize("O3")

#if __has_include("platform/mbed_mpu_mgmt.h")
#include "platform/mbed_mpu_mgmt.h"
#endif

#define N      4096
#define PASSES 100

typedef unsigned int (*jitfn0)();
typedef unsigned int (*jitfn1)(unsigned int);

// ---- v2: 원본 포팅 (ldr r0,[pc,#4]; lsls/lsrs; bx lr; nop; value) ----
alignas(4) static uint16_t code[6] = {
    0x4801, 0x0000, 0x4770, 0x46C0, 0x0010, 0x0000
};
static jitfn0 func;

#define SGN(X)   ((int32_t)(X) >> 31)
#define SHIFT(X) ((uint16_t)(0x0800 & SGN(X)))
#define ABS(X)   ((uint32_t)((((int32_t)(X)) + SGN(X)) ^ SGN(X)))

// ---- v3: 인자 전달 JIT (lsls/lsrs r0,r0,#n; bx lr) + 명령어 LUT ----
alignas(4) static uint16_t code2[2] = { 0x0000, 0x4770 };
static uint16_t ilut[41];
static jitfn1 func2;

// ---- v4: 시프트 양별 함수 41개를 미리 생성 ----
alignas(4) static uint16_t farm[41][2];
static jitfn1 ftab[41];

// ---- v6: 데이터 특화 JIT — sh[] 전체를 전개한 명령어 스트림 ----
// 원소당: ldmia r0!,{r3} / lsls|lsrs r3,r3,#n / adds r2,r2,r3
// 프롤로그: movs r2,#0  에필로그: movs r0,r2 / bx lr
alignas(4) static uint16_t stream[3 * N + 4];
typedef unsigned int (*jitfnN)(const uint32_t*);
static jitfnN fstream;

static uint32_t val[N];
static int8_t sh[N];
static volatile uint32_t sink;

unsigned int shift_origin(unsigned int value, int8_t s) {
    if (s > 0) return value << s;
    else       return value >> -s;
}

unsigned int shift_memory(unsigned int value, int8_t s) {
    memcpy((void*)&code[4], &value, 4);
    code[1] = SHIFT(s) | (ABS(s) << 6);
    return func();
}

static inline unsigned int shift_memory2(unsigned int value, int8_t s) {
    code2[0] = ilut[s + 20];
    return func2(value);
}

static inline unsigned int shift_table(unsigned int value, int8_t s) {
    return ftab[s + 20](value);
}

static inline unsigned int shift_branchless(unsigned int value, int8_t s) {
    uint32_t m = (uint32_t)SGN(s);
    uint32_t l = value << ((uint32_t)s & 31);
    uint32_t r = value >> ((uint32_t)-s & 31);
    return (l & ~m) | (r & m);
}

// sh[] 배열에 특화된 코드를 생성하고 컴파일 소요 시간(us)을 반환
static uint32_t jit_compile_stream() {
    uint32_t t0 = micros();
    uint16_t* p = stream;
    *p++ = 0x2200;                       // movs r2, #0
    for (int i = 0; i < N; i++) {
        int s = sh[i];
        int n = s < 0 ? -s : s;
        *p++ = 0xC808;                   // ldmia r0!, {r3}
        *p++ = (uint16_t)((s < 0 ? 0x0800 : 0x0000) | (n << 6) | 0x001B);  // lsls/lsrs r3,r3,#n
        *p++ = 0x18D2;                   // adds r2, r2, r3
    }
    *p++ = 0x0010;                       // movs r0, r2
    *p++ = 0x4770;                       // bx lr
    fstream = (jitfnN)((uintptr_t)stream | 1);
    return micros() - t0;
}

static uint32_t rng_state = 0x12345678u;
static inline uint32_t xrand() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// 벤치 하니스: PRE에서 전역을 지역으로 호이스팅, EXPR은 지역만 사용
#define BENCH(name, PRE, EXPR)                                   \
    do {                                                         \
        PRE;                                                     \
        const uint32_t* pv = val;                                \
        const int8_t*   ps = sh;                                 \
        uint32_t acc = 0;                                        \
        uint32_t t0 = micros();                                  \
        for (int p = 0; p < PASSES; p++)                         \
            for (int i = 0; i < N; i++) {                        \
                uint32_t v = pv[i]; int8_t s = ps[i];            \
                acc += (EXPR);                                   \
            }                                                    \
        uint32_t t1 = micros();                                  \
        sink = acc;                                              \
        uint32_t total = (uint32_t)PASSES * N;                   \
        Serial.print(name); Serial.print(",");                   \
        Serial.print((t1 - t0) / 1000); Serial.print("ms,");     \
        Serial.print((float)(t1 - t0) * 1000.0f / total, 1);     \
        Serial.println("ns/op");                                 \
    } while (0)

static void verify(const char* name, unsigned int (*f)(unsigned int, int8_t)) {
    int mismatch = 0;
    for (int i = 0; i < N; i++)
        if (f(val[i], sh[i]) != shift_origin(val[i], sh[i])) mismatch++;
    Serial.print("VERIFY,"); Serial.print(name);
    Serial.print(",mismatch="); Serial.println(mismatch);
}

static unsigned int w_memory2(unsigned int v, int8_t s)    { return shift_memory2(v, s); }
static unsigned int w_table(unsigned int v, int8_t s)      { return shift_table(v, s); }
static unsigned int w_branchless(unsigned int v, int8_t s) { return shift_branchless(v, s); }

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    delay(500);

#if __has_include("platform/mbed_mpu_mgmt.h")
    mbed_mpu_manager_lock_ram_execution();
#endif
    func  = (jitfn0)((uintptr_t)code  | 1);
    func2 = (jitfn1)((uintptr_t)code2 | 1);

    for (int k = 0; k < 41; k++) {
        int s = k - 20;
        int n = s < 0 ? -s : s;
        uint16_t instr = (uint16_t)((s < 0 ? 0x0800 : 0x0000) | (n << 6));
        ilut[k]    = instr;
        farm[k][0] = instr;
        farm[k][1] = 0x4770;  // bx lr
        ftab[k]    = (jitfn1)((uintptr_t)&farm[k][0] | 1);
    }

    for (int i = 0; i < N; i++) {
        val[i] = xrand() % 90 + 10;
        sh[i]  = (int8_t)(xrand() % 40) - 20;
    }

    verify("memory",     shift_memory);
    verify("memory2",    w_memory2);
    verify("table",      w_table);
    verify("branchless", w_branchless);

    BENCH("baseline",   ;, v ^ (uint32_t)s);
    BENCH("origin",     ;, shift_origin(v, s));
    BENCH("memory",     ;, shift_memory(v, s));
    BENCH("memory2",
          uint16_t* c2 = code2; const uint16_t* lut = ilut; jitfn1 f2 = func2,
          (c2[0] = lut[s + 20], f2(v)));
    BENCH("table",
          jitfn1* tb = ftab,
          tb[s + 20](v));
    BENCH("branchless", ;, shift_branchless(v, s));

    // v6: 데이터 특화 스트림 — 정합성(합계 비교) 후 측정
    uint32_t gen_us = jit_compile_stream();
    uint32_t ref = 0;
    for (int i = 0; i < N; i++) ref += shift_origin(val[i], sh[i]);
    uint32_t got = fstream(val);
    Serial.print("VERIFY,stream,");
    Serial.println(ref == got ? "mismatch=0" : "MISMATCH");
    Serial.print("JIT_COMPILE,"); Serial.print(gen_us); Serial.println("us");
    {
        uint32_t acc = 0;
        uint32_t t0 = micros();
        for (int p = 0; p < PASSES; p++) acc += fstream(val);
        uint32_t t1 = micros();
        sink = acc;
        uint32_t total = (uint32_t)PASSES * N;
        Serial.print("stream,");
        Serial.print((t1 - t0) / 1000); Serial.print("ms,");
        Serial.print((float)(t1 - t0) * 1000.0f / total, 1);
        Serial.println("ns/op");
    }
    Serial.println("DONE");
}

void loop() { delay(1000); }
