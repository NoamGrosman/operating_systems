cmake_minimum_required(VERSION 4.0)
project(smash)

set(CMAKE_CXX_STANDARD 11)

add_executable(
        smash/jobs.h
        smash/jobs.cpp
        smash/smash.c)
