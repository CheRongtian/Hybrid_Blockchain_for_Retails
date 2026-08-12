#!/usr/bin/env python3
"""Benchmark the two allocator modules without starting either server."""

from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile


ALLOCATORS = (
    "new_delete",
    "memory_pool",
    "malloc_free",
    "conmem_pool",
)


BENCHMARK_SOURCE = r"""
#include "conmem_pool.hpp"
#include "mempool.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <vector>

struct alignas(16) Block {
    std::uint64_t words[8];
};

#if defined(__clang__) || defined(__GNUC__)
#define BENCHMARK_NOINLINE __attribute__((noinline))
#else
#define BENCHMARK_NOINLINE
#endif

struct RunResult {
    double elapsed_ms;
    bool failed;
};

using AllocateFn = void* (*)(void* context);
using DeallocateFn = void (*)(void* context, void* memory);

struct AllocatorApi {
    void* context;
    AllocateFn allocate;
    DeallocateFn deallocate;
};

BENCHMARK_NOINLINE void* AllocateNew(void*)
{
    return ::operator new(sizeof(Block), std::nothrow);
}

BENCHMARK_NOINLINE void DeallocateNew(void*, void* memory)
{
    ::operator delete(memory);
}

BENCHMARK_NOINLINE void* AllocateMemoryPool(void* context)
{
    return static_cast<MemoryPool*>(context)->Allocate();
}

BENCHMARK_NOINLINE void DeallocateMemoryPool(void* context, void* memory)
{
    static_cast<MemoryPool*>(context)->Free(memory);
}

BENCHMARK_NOINLINE void* AllocateMalloc(void*)
{
    return std::malloc(sizeof(Block));
}

BENCHMARK_NOINLINE void DeallocateMalloc(void*, void* memory)
{
    std::free(memory);
}

BENCHMARK_NOINLINE void* AllocateConMem(void*)
{
    return conmem::allocate(sizeof(Block));
}

BENCHMARK_NOINLINE void DeallocateConMem(void*, void* memory)
{
    conmem::deallocate(memory, sizeof(Block));
}

BENCHMARK_NOINLINE std::uint64_t TouchBlock(void* memory, std::uint64_t value)
{
    volatile auto* block = static_cast<volatile Block*>(memory);
    block->words[0] = value;
    return block->words[0];
}

BENCHMARK_NOINLINE RunResult RunCase(
    int thread_count,
    std::size_t allocations_per_thread,
    const AllocatorApi& api)
{
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> checksum{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(thread_count));

    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&, thread_index]() {
            void* warmup = nullptr;
            try {
                warmup = api.allocate(api.context);
            } catch (...) {
                failed.store(true, std::memory_order_relaxed);
            }
            if (!warmup) {
                failed.store(true, std::memory_order_relaxed);
            } else {
                api.deallocate(api.context, warmup);
            }

            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            const std::uint64_t seed = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) ^
                static_cast<std::uint64_t>(thread_index);
            std::uint64_t local_checksum = seed;
            for (std::size_t index = 0; index < allocations_per_thread; ++index) {
                void* memory = nullptr;
                try {
                    memory = api.allocate(api.context);
                } catch (...) {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
                if (!memory) {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }

                const std::uint64_t value = static_cast<std::uint64_t>(index) ^
                    local_checksum ^ seed;
                local_checksum ^= TouchBlock(memory, value);
                api.deallocate(api.context, memory);
            }
            checksum.fetch_add(local_checksum, std::memory_order_relaxed);
        });
    }

    while (ready.load(std::memory_order_acquire) != thread_count) {
        std::this_thread::yield();
    }

    const auto start_time = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    const auto end_time = std::chrono::steady_clock::now();

    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    if (checksum.load(std::memory_order_relaxed) == 0xFFFFFFFFFFFFFFFFULL) {
        std::cerr << "checksum: " << checksum.load() << std::endl;
    }

    return {elapsed_ms, failed.load(std::memory_order_relaxed)};
}

bool ParsePositiveInt(const char* value, int& result)
{
    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0) return false;
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParsePositiveSize(const char* value, std::size_t& result)
{
    try {
        const auto parsed = std::stoull(value);
        if (parsed == 0) return false;
        result = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char** argv)
{
    std::string allocator_name;
    int thread_count = 1;
    std::size_t allocations_per_thread = 100000;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--allocator" && index + 1 < argc) {
            allocator_name = argv[++index];
        } else if (argument == "--threads" && index + 1 < argc) {
            if (!ParsePositiveInt(argv[++index], thread_count)) {
                std::cerr << "Invalid thread count" << std::endl;
                return 2;
            }
        } else if (argument == "--allocations" && index + 1 < argc) {
            if (!ParsePositiveSize(argv[++index], allocations_per_thread)) {
                std::cerr << "Invalid allocation count" << std::endl;
                return 2;
            }
        } else {
            std::cerr << "Usage: --allocator <name> --threads <count> "
                         "--allocations <count>" << std::endl;
            return 2;
        }
    }

    if (allocator_name.empty()) {
        std::cerr << "Missing allocator name" << std::endl;
        return 2;
    }

    RunResult result{0.0, false};
    if (allocator_name == "new_delete") {
        result = RunCase(
            thread_count,
            allocations_per_thread,
            {nullptr, &AllocateNew, &DeallocateNew});
    } else if (allocator_name == "memory_pool") {
        MemoryPool pool(sizeof(Block), alignof(Block), 1024);
        result = RunCase(
            thread_count,
            allocations_per_thread,
            {&pool, &AllocateMemoryPool, &DeallocateMemoryPool});
    } else if (allocator_name == "malloc_free") {
        result = RunCase(
            thread_count,
            allocations_per_thread,
            {nullptr, &AllocateMalloc, &DeallocateMalloc});
    } else if (allocator_name == "conmem_pool") {
        result = RunCase(
            thread_count,
            allocations_per_thread,
            {nullptr, &AllocateConMem, &DeallocateConMem});
    } else {
        std::cerr << "Unknown allocator: " << allocator_name << std::endl;
        return 2;
    }

    const double total_operations = static_cast<double>(thread_count) *
        static_cast<double>(allocations_per_thread);
    const double operations_per_second = total_operations /
        (result.elapsed_ms / 1000.0);
    std::cout << "RESULT|" << allocator_name << "|" << thread_count << "|"
              << allocations_per_thread << "|" << result.elapsed_ms << "|"
              << operations_per_second << "|" << (result.failed ? 1 : 0)
              << std::endl;
    return result.failed ? 1 : 0;
}
"""


@dataclass(frozen=True)
class BenchmarkResult:
    elapsed_ms: float
    operations_per_second: float


def find_compiler(requested: str | None) -> list[str]:
    if requested:
        return shlex.split(requested)

    for candidate in ("clang++", "c++"):
        compiler = shutil.which(candidate)
        if compiler:
            return [compiler]

    raise RuntimeError("No C++ compiler found. Set CXX or use --compiler.")


def compile_benchmark(project_root: Path, output_path: Path, compiler: list[str]) -> None:
    source_path = output_path.with_suffix(".cpp")
    source_path.write_text(BENCHMARK_SOURCE, encoding="utf-8")

    private_chain_dir = project_root / "Code" / "PrivateChain"
    memory_pool_dir = private_chain_dir / "MemoryPool"
    conmem_pool_dir = private_chain_dir / "ConMemPool"
    command = [
        *compiler,
        "-std=c++17",
        "-O2",
        "-pthread",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        f"-I{memory_pool_dir}",
        f"-I{conmem_pool_dir}",
        str(source_path),
        str(memory_pool_dir / "mempool.cpp"),
        str(conmem_pool_dir / "concurrency_mempool.cpp"),
        "-o",
        str(output_path),
    ]
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(
            "Benchmark compilation failed:\n"
            + completed.stdout
            + completed.stderr
        )


def run_case(
    executable: Path,
    allocator: str,
    threads: int,
    allocations: int,
) -> BenchmarkResult:
    completed = subprocess.run(
        [
            str(executable),
            "--allocator",
            allocator,
            "--threads",
            str(threads),
            "--allocations",
            str(allocations),
        ],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"Benchmark case failed for {allocator} at {threads} thread(s):\n"
            + completed.stdout
            + completed.stderr
        )

    result_line = next(
        (line for line in completed.stdout.splitlines() if line.startswith("RESULT|")),
        None,
    )
    if result_line is None:
        raise RuntimeError(f"No benchmark result returned for {allocator}.")

    fields = result_line.split("|")
    if len(fields) != 7 or fields[1] != allocator or fields[6] != "0":
        raise RuntimeError(f"Invalid benchmark result: {result_line}")

    return BenchmarkResult(
        elapsed_ms=float(fields[4]),
        operations_per_second=float(fields[5]),
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare standard allocation with MemoryPool and ConMemPool. "
            "This is a local allocator benchmark; it does not start a server."
        )
    )
    parser.add_argument(
        "--allocations",
        type=int,
        default=100000,
        help="allocation/free operations per thread (default: 100000)",
    )
    parser.add_argument(
        "--threads",
        type=int,
        nargs="+",
        default=[1, 8, 16],
        help="thread counts to compare (default: 1 8 16)",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=3,
        help="runs per allocator/thread pair; median is reported (default: 3)",
    )
    parser.add_argument(
        "--compiler",
        help="C++ compiler command, or leave unset to use clang++/c++",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.allocations <= 0:
        raise ValueError("--allocations must be positive")
    if args.repeat <= 0:
        raise ValueError("--repeat must be positive")
    if any(thread_count <= 0 for thread_count in args.threads):
        raise ValueError("every value in --threads must be positive")


def print_results(
    results: dict[tuple[str, int], list[BenchmarkResult]],
    thread_counts: list[int],
) -> None:
    print()
    print("Allocator benchmark results")
    print("Higher operations/second is better.")
    print()
    print(
        f"{'Threads':>7}  {'new/delete':>14}  {'MemoryPool':>14}  "
        f"{'Speedup':>9}  {'malloc/free':>14}  {'ConMemPool':>14}  "
        f"{'Speedup':>9}"
    )
    print("-" * 94)

    for thread_count in thread_counts:
        medians = {
            allocator: statistics.median(
                item.operations_per_second
                for item in results[(allocator, thread_count)]
            )
            for allocator in ALLOCATORS
        }
        memory_pool_speedup = medians["memory_pool"] / medians["new_delete"]
        conmem_speedup = medians["conmem_pool"] / medians["malloc_free"]
        print(
            f"{thread_count:>7}  "
            f"{medians['new_delete']:>14,.0f}  "
            f"{medians['memory_pool']:>14,.0f}  "
            f"{memory_pool_speedup:>8.2f}x  "
            f"{medians['malloc_free']:>14,.0f}  "
            f"{medians['conmem_pool']:>14,.0f}  "
            f"{conmem_speedup:>8.2f}x"
        )

    print()
    print("Speedup > 1.00x means the module allocator was faster in this run.")
    print("This measures allocation/free throughput only, not HTTP, SQLite, or IPFS.")


def main() -> int:
    args = parse_args()
    try:
        validate_args(args)
        project_root = Path(__file__).resolve().parent
        compiler = find_compiler(args.compiler or os.environ.get("CXX"))
    except (RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2

    print(
        f"Compiling local allocator benchmark with {' '.join(compiler)}..."
    )
    print(
        f"Running {args.repeat} repetition(s) for {args.allocations:,} "
        f"allocation/free operations per thread."
    )

    results: dict[tuple[str, int], list[BenchmarkResult]] = {}
    try:
        with tempfile.TemporaryDirectory(prefix="allocator_benchmark_") as temp_dir:
            temporary_root = Path(temp_dir)
            executable = temporary_root / "allocator_benchmark"
            compile_benchmark(project_root, executable, compiler)

            for thread_count in args.threads:
                for allocator in ALLOCATORS:
                    key = (allocator, thread_count)
                    results[key] = []
                    for _ in range(args.repeat):
                        results[key].append(
                            run_case(
                                executable,
                                allocator,
                                thread_count,
                                args.allocations,
                            )
                        )
                print(f"Completed {thread_count} thread(s).")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1

    print_results(results, args.threads)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
