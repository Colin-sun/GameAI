// opemMP 并行随机数生成器
#pragma once
#include <omp.h>
#include <chrono>
#include <random>
#include <array>
#include <unistd.h>   // getpid

// 128 bit 熵
inline std::array<uint32_t,4> unique_seed_128()
{
    using namespace std::chrono;
    uint64_t t   = static_cast<uint64_t>(
                       high_resolution_clock::now()
                       .time_since_epoch().count());
    uint64_t pid = static_cast<uint64_t>(getpid());   // 进程级唯一
    uint64_t tid = static_cast<uint64_t>(omp_get_thread_num());

    return { uint32_t(t),
             uint32_t(t >> 32),
             uint32_t(pid ^ tid),
             uint32_t((pid >> 32) ^ (tid >> 32)) };
}

// 每个线程只构造一次随机数引擎
inline std::mt19937& threaded_rng()
{
    thread_local std::mt19937 rng = []{
        auto seed_data = unique_seed_128();
        std::seed_seq seq(seed_data.begin(), seed_data.end());
        return std::mt19937(seq);
    }();
    return rng;
}
