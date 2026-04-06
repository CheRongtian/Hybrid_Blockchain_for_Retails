import subprocess
import re
import statistics
import matplotlib.pyplot as plt
import sys
import time

RUNS = 100
TRIM_PERCENT = 0.05

def run_command(cmd, show_output=True):
    try:
        # Use Python's high-resolution timer to bypass macOS 10ms limitation
        start_time = time.perf_counter()
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)
        end_time = time.perf_counter()
        
        output = result.stderr
        
        # Extract memory from macOS time -l
        mem_match = re.search(r'(\d+)\s+maximum resident set size', output)
        mem_bytes = int(mem_match.group(1)) if mem_match else 0
        
        time_ms = (end_time - start_time) * 1000
        space_mb = mem_bytes / (1024 * 1024)
        
        return time_ms, space_mb
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {cmd}\nError: {e.stderr}")
        sys.exit(1)

def process_data(data_list):
    """Clean data: sort, trim outliers, and calculate mean."""
    data_list.sort()
    trim_count = int(RUNS * TRIM_PERCENT)
    if trim_count > 0:
        cleaned_data = data_list[trim_count:-trim_count]
    else:
        cleaned_data = data_list
    return statistics.mean(cleaned_data)

def draw_chart(mempool_t, std_t, mempool_s, std_s):
    """Draw and save comparison charts using matplotlib."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 6))

    labels_time = ['MemoryPool', 'Standard new']
    values_time = [mempool_t, std_t]
    colors_time = ['#4CAF50', '#F44336'] 
    
    ax1.bar(labels_time, values_time, color=colors_time, width=0.5)
    ax1.set_title('Time Performance (ms) - Lower is Better', fontsize=14)
    ax1.set_ylabel('Milliseconds (ms)')
    for i, v in enumerate(values_time):
        ax1.text(i, v + (max(values_time)*0.02), f"{v:.2f}", ha='center', fontweight='bold')

    labels_space = ['MemoryPool', 'Standard new']
    values_space = [mempool_s, std_s]
    colors_space = ['#2196F3', '#FF9800'] 
    
    ax2.bar(labels_space, values_space, color=colors_space, width=0.5)
    ax2.set_title('Space Utilization (MB) - Lower is Better', fontsize=14)
    ax2.set_ylabel('Megabytes (MB)')
    for i, v in enumerate(values_space):
        ax2.text(i, v + (max(values_space)*0.02), f"{v:.2f}", ha='center', fontweight='bold')

    plt.tight_layout()
    plt.savefig('benchmark_results.png', dpi=300)
    print("\nVisual chart saved to current directory: benchmark_results.png")

if __name__ == "__main__":
    print("======================================")
    print("1. Compiling C++ benchmark programs...")
    print("======================================")
    
    subprocess.run("clang++ -O3 -pthread concurrency_mempool.cpp -o concurrency_mempool", shell=True, check=True)
    subprocess.run("clang++ -O3 -pthread -fno-builtin baseline.cpp -o baseline", shell=True, check=True)
    
    print(f"\n======================================")
    print(f"2. Running benchmarks ({RUNS} iterations, please wait...)")
    print(f"======================================")
    
    m_times, s_times = [], []
    m_spaces, s_spaces = [], []

    for i in range(RUNS):
        progress = int((i+1)/RUNS * 20)
        sys.stdout.write(f"\rProgress: [{'='*progress}{' '*(20-progress)}] {i+1}/{RUNS}")
        sys.stdout.flush()

        m_t, m_s = run_command("/usr/bin/time -l ./concurrency_mempool", show_output=False)
        m_times.append(m_t)
        m_spaces.append(m_s)

        s_t, s_s = run_command("/usr/bin/time -l ./baseline", show_output=False)
        s_times.append(s_t)
        s_spaces.append(s_s)
    
    print("\n\n======================================")
    print("3. Data Processing & Statistical Results")
    print("======================================")
    avg_m_time = process_data(m_times)
    avg_s_time = process_data(s_times)
    avg_m_space = process_data(m_spaces)
    avg_s_space = process_data(s_spaces)

    print(f"[ Time Performance ]")
    print(f"  Mempool Avg Time   : {avg_m_time:.2f} ms")
    print(f"  Standard Avg Time  : {avg_s_time:.2f} ms")
    if avg_m_time > 0:
        print(f"  Speedup Factor     : {avg_s_time / avg_m_time:.2f} x\n")

    print(f"[ Space Utilization ]")
    print(f"  Mempool Avg Space  : {avg_m_space:.2f} MB")
    print(f"  Standard Avg Space : {avg_s_space:.2f} MB")
    print(f"  Memory Saved       : {avg_s_space - avg_m_space:.2f} MB")
    if avg_s_space > 0:
        print(f"  Memory Saved (%)   : {(avg_s_space - avg_m_space) / avg_s_space * 100:.2f}%\n")

    draw_chart(avg_m_time, avg_s_time, avg_m_space, avg_s_space)