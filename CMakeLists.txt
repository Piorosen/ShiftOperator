cmake_minimum_required(VERSION 3.10)

# 프로젝트 이름 설정
project(MyProject)

# C++ 표준 설정 (예: C++11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# main.cpp 파일을 사용하여 실행 파일 생성
add_executable(MyProject main.cpp)
