import subprocess
import re
import statistics
import matplotlib.pyplot as plt
import sys

# Number of test runs
RUNS = 100
# Trim percentage for outliers (top and bottom 5%)
TRIM_PERCENT = 0.05

def run_command(cmd, show_output=True):
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)
        if show_output:
            print(result.stdout.strip())
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {cmd}\nError: {e.stderr}")
        sys.exit(1)

def extract_data(output):
    """Extract data from C++ output using regex, supporting both time and space formats."""
    try:
        # Match number in "1. xxxxx: 12.34"
        val1 = float(re.search(r'1\..*?(\d+\.?\d*)', output).group(1))
        # Match number in "2. xxxxx: 12.34"
        val2 = float(re.search(r'2\..*?(\d+\.?\d*)', output).group(1))
        return val1, val2
    except AttributeError:
        print("Failed to parse output. Ensure C++ output contains '1. xxx: number' and '2. xxx: number'.")
        sys.exit(1)

def process_data(data_list):
    """Clean data: sort, trim outliers, and calculate mean."""
    data_list.sort()
    trim_count = int(RUNS * TRIM_PERCENT)
    if trim_count > 0:
        # Remove highest and lowest extreme values
        cleaned_data = data_list[trim_count:-trim_count]
    else:
        cleaned_data = data_list
    return statistics.mean(cleaned_data)

def draw_chart(mempool_t, std_t, mempool_s, std_s):
    """Draw and save comparison charts using matplotlib."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 6))

    # Time performance chart (Lower is Better)
    labels_time = ['MemoryPool', 'Standard new']
    values_time = [mempool_t, std_t]
    colors_time = ['#4CAF50', '#F44336'] # Green vs Red
    
    ax1.bar(labels_time, values_time, color=colors_time, width=0.5)
    ax1.set_title('Time Performance (ms) - Lower is Better', fontsize=14)
    ax1.set_ylabel('Milliseconds (ms)')
    for i, v in enumerate(values_time):
        ax1.text(i, v + (max(values_time)*0.02), f"{v:.2f}", ha='center', fontweight='bold')

    # Space utilization chart (Lower is Better)
    labels_space = ['MemoryPool', 'Standard new']
    values_space = [mempool_s, std_s]
    colors_space = ['#2196F3', '#FF9800'] # Blue vs Orange
    
    ax2.bar(labels_space, values_space, color=colors_space, width=0.5)
    ax2.set_title('Space Utilization (MB) - Lower is Better', fontsize=14)
    ax2.set_ylabel('Megabytes (MB)')
    for i, v in enumerate(values_space):
        ax2.text(i, v + (max(values_space)*0.02), f"{v:.2f}", ha='center', fontweight='bold')

    # Adjust layout and save
    plt.tight_layout()
    plt.savefig('benchmark_results.png', dpi=300)
    print("Visual chart saved to current directory: benchmark_results.png")

if __name__ == "__main__":
    print("======================================")
    print("1. Compiling C++ benchmark programs...")
    print("======================================")
    run_command("clang++ -O3 -o spacetest mempool.cpp spacetest.cpp")
    run_command("clang++ -O3 -o timetest mempool.cpp timetest.cpp")
    
    print(f"\n======================================")
    print(f"2. Running benchmarks ({RUNS} iterations, please wait...)")
    print(f"======================================")
    
    m_times, s_times = [], []
    m_spaces, s_spaces = [], []

    for i in range(RUNS):
        # Print progress bar
        progress = int((i+1)/RUNS * 20)
        sys.stdout.write(f"\rProgress: [{'='*progress}{' '*(20-progress)}] {i+1}/{RUNS}")
        sys.stdout.flush()

        # Run time test (Regex mapping: 1 is Mempool, 2 is Standard)
        time_out = run_command("./timetest", show_output=False)
        m_t, s_t = extract_data(time_out)
        m_times.append(m_t)
        s_times.append(s_t)

        # Run space test (Regex mapping: 1 is Standard, 2 is Mempool based on previous C++ code)
        space_out = run_command("./spacetest", show_output=False)
        s_s, m_s = extract_data(space_out)
        s_spaces.append(s_s)
        m_spaces.append(m_s)
    
    print("\n\n======================================")
    print("3. Data Processing & Statistical Results")
    print("======================================")
    avg_m_time = process_data(m_times)
    avg_s_time = process_data(s_times)
    avg_m_space = process_data(m_spaces)
    avg_s_space = process_data(s_spaces)

    print(f"[ Time Performance]")
    print(f"  Mempool Avg Time   : {avg_m_time:.2f} ms")
    print(f"  Standard Avg Time  : {avg_s_time:.2f} ms")
    print(f"  Speedup Factor  : {avg_s_time / avg_m_time:.2f} x\n")

    print(f"[ Space Utilization]")
    print(f"  Mempool Avg Space  : {avg_m_space:.2f} MB")
    print(f"  Standard Avg Space : {avg_s_space:.2f} MB")
    print(f"  Memory Saved    : {avg_s_space - avg_m_space:.2f} MB\n")

    # 4. Draw charts
    draw_chart(avg_m_time, avg_s_time, avg_m_space, avg_s_space)