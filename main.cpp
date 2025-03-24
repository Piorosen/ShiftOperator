#include <stdio.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

#define SHIFT(X) (224 ^ ((224 ^ 248) & (((X >> (sizeof(X) << 2) - 1) << 31) >> 31)))
#define ABS(X) (X + (X >> (sizeof(X) * 8 - 1))) ^ (X >> (sizeof(X) * 8 - 1))

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <UInt32 value> <Int8 shift indicator>\n", argv[0]);
        return 1;
    }

    // 입력값 파싱
    unsigned int value = (unsigned int)atoi(argv[1]);
    char shiftIndicator = atoi(argv[2]);  // Int8 값으로 취급

    unsigned char code[] = {
        0xB8,                   // mov eax, imm32
        0x10, 0x00, 0x00, 0x00, // value 자리 (UInt32)
        0xC1, 0x00, // 0xE0, 0xF8             // 기본: SHL EAX, imm8 (E0)
        0x00,                   // shift count (예시로 4비트 시프트)
        0xC3                    // ret
    };

    // 0xE0 : << 
    // 0xF8 : >> 
    memcpy(&code[1], &value, 4);
    code[6] = SHIFT(shiftIndicator);
    code[7] = ABS(shiftIndicator);
    printf("%d %d ", code[6], code[7]);
    // memcpy(&code[7], &val, 1);

    // 실행 가능한 메모리 영역 할당 및 코드 복사
    void* mem = mmap(NULL, sizeof(code), PROT_WRITE | PROT_READ,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
    if(mem == MAP_FAILED) {
         perror("mmap");
         return 1;
    }

    
    memcpy(mem, code, sizeof(code));
    if(mprotect(mem, sizeof(code), PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        perror("mprotect");
        return 0;
    }
    
    // ((char*)mem)[7] = 0; // **** 중간에 코드 수정 ****
    // JIT 컴파일된 함수를 호출
    unsigned int (*func)() = reinterpret_cast<unsigned int (*)()>(mem);
    unsigned int result = func();

    printf("Result: %u\n", result);
    return 0;
}
