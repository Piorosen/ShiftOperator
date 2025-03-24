#include <stdio.h>
#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <chrono>
#include <random>

#define SHIFT(X) (224 ^ ((224 ^ 248) & (((X >> (sizeof(X) << 2) - 1) << 31) >> 31)))
#define ABS(X) (X + (X >> (sizeof(X) * 8 - 1))) ^ (X >> (sizeof(X) * 8 - 1))

unsigned char code[] = {
    0xB8,                   // mov eax, imm32
    0x10, 0x00, 0x00, 0x00, // value 자리 (UInt32)
    0xC1, 0x00, // 0xE0, 0xF8             // 기본: SHL EAX, imm8 (E0)
    0x00,                   // shift count (예시로 4비트 시프트)
    0xC3                    // ret
};

unsigned int (*func)();
void* mem;

unsigned int shift_origin(unsigned int value, char shiftIndicator) {
    if (shiftIndicator > 0) {
        return value << shiftIndicator;
    } else {
        return value >> -shiftIndicator;
    }
}

unsigned int shift_memory(unsigned int value, char shiftIndicator) {
    // 0xE0 : << 
    // 0xF8 : >> 
    memcpy(&((char*)mem)[1], &value, 4);
    ((char*)mem)[6] = SHIFT(shiftIndicator);
    ((char*)mem)[7] = ABS(shiftIndicator);
    return func();
}

decltype(auto) benchmark_origin(unsigned int* value, char* shiftIndicator) {
    auto s = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10000000; i++) {
            shift_origin(value[i], shiftIndicator[i]);
        }
    }
    auto e = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
}

decltype(auto) benchmark_memory(unsigned int* value, char* shiftIndicator) {
    auto s = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10000000; i++) {
            memcpy(&((char*)mem)[1], &value[i], 4);
            ((char*)mem)[6] = SHIFT(shiftIndicator[i]);
            ((char*)mem)[7] = ABS(shiftIndicator[i]);
            func();
        }
    }
    auto e = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
}

unsigned int* random_value_int(int size, int s, int e) {
    unsigned int* arr = new unsigned int[size];
    auto rd = std::random_device{};
    for (int i = 0; i < size; i++) {
        arr[i] = rd() % (e - s) + s;
    }
    return arr;
}

char* random_value_char(int size, int s, int e) {
    char* arr = new char[size];
    auto rd = std::random_device{};
    for (int i = 0; i < size; i++) {
        arr[i] = rd() % (e - s) + s;
    }
    return arr;
}

int main(int argc, char* argv[]) {
    // 실행 가능한 메모리 영역 할당 및 코드 복사
    mem = mmap(NULL, sizeof(code), PROT_WRITE | PROT_READ,
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
    func = reinterpret_cast<unsigned int (*)()>(mem);

    auto value = random_value_int(10000000, 100, 100000);
    auto shiftIndicator = random_value_char(10000000, -10, 10);

    std::cout << "shift_origin: " << benchmark_origin(value, shiftIndicator) << std::endl;
    std::cout << "shift_memory: " << benchmark_memory(value, shiftIndicator) << std::endl;

    return 0;
}
