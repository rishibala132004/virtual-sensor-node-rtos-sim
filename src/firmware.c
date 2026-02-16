#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

typedef void (*task_fn_t)(void);

typedef struct {
    const char* name;
    task_fn_t run;
    uint32_t period_ms;
    uint32_t next_release_ms;
    float power_mw;  // power when executing
    uint8_t enabled;
} task_t;

// Global simulated time (ms)
static uint32_t now_ms = 0;

// Global sensor state
static float g_temp_c = 25.0f;
static float g_accel = 0.0f;
static float g_temp_filtered = 25.0f;

// Energy tracking
static double g_total_energy_mj = 0.0;  // milli-joules

static const float TASK_EXEC_TIME_MS = 1.0f;

// Timing stats (static allocation)
static uint32_t g_tick_count = 0;
static uint32_t g_task_exec_total[4] = {0};
static uint32_t g_task_response_total[4] = {0};
static uint32_t g_task_jitter_sq[4] = {0};  // for stddev
static uint32_t g_task_misses[4] = {0};
static uint32_t g_task_releases[4] = {0};
static uint32_t g_cpu_idle_ms = 0;
static const uint32_t WCET_US[4] = {2000, 1500, 2500, 5000}; // example worst-case exec (us)

// Ring buffer IPC
#define BUF_SIZE 16
typedef struct { float temp, accel, filtered; uint32_t timestamp; } sensor_pkt_t;
static sensor_pkt_t g_tx_ring[BUF_SIZE];  // Static buffer
static uint8_t g_tx_head = 0, g_tx_tail = 0;
static uint32_t g_tx_drops = 0;

// Simple random helpers
static float randf(float min, float max) {
    return min + (float)rand() / (float)RAND_MAX * (max - min);
}

// TASKS
static void task_sensor_temp(void) {
    // Simulate slow drift + noise
    float drift = randf(-0.01f, 0.01f);
    float noise = randf(-0.1f, 0.1f);
    g_temp_c += drift + noise;
    printf("%6u ms TEMP: Temp reading %.2f C\n", now_ms, g_temp_c);

    // Push to ring buffer
    if ((g_tx_head + 1) % BUF_SIZE != g_tx_tail) {  // not full
        g_tx_ring[g_tx_head].temp = g_temp_c;
        g_tx_ring[g_tx_head].timestamp = now_ms;
        g_tx_head = (g_tx_head + 1) % BUF_SIZE;
    } else g_tx_drops++;
}

static void task_sensor_accel(void) {
    float noise = randf(-0.2f, 0.2f);
    g_accel = fminf(fmaxf(sin(now_ms * 0.001f) + noise, -2.0f), 2.0f);  // ±2g
    printf("%6u ms ACCEL: Accel reading %.3f g\n", now_ms, g_accel);

    // Push to ring buffer
    if ((g_tx_head + 1) % BUF_SIZE != g_tx_tail) {
        g_tx_ring[g_tx_head].accel = g_accel;
        g_tx_ring[g_tx_head].timestamp = now_ms;
        g_tx_head = (g_tx_head + 1) % BUF_SIZE;
    } else g_tx_drops++;
}

static void task_background(void) {
    // Simple exponential moving average filter
    const float alpha = 0.1f;
    g_temp_filtered = alpha * g_temp_c + (1.0f - alpha) * g_temp_filtered;
    printf("%6u ms BG: Filtered temp %.2f C\n", now_ms, g_temp_filtered);

    // Push to ring buffer
    if ((g_tx_head + 1) % BUF_SIZE != g_tx_tail) {
        g_tx_ring[g_tx_head].filtered = g_temp_filtered;
        g_tx_ring[g_tx_head].timestamp = now_ms;
        g_tx_head = (g_tx_head + 1) % BUF_SIZE;
    } else g_tx_drops++;
}

static void task_comm(void) {
    if (g_tx_head != g_tx_tail) {  // has data
        sensor_pkt_t pkt = g_tx_ring[g_tx_tail];
        printf("%6u ms COMM: TX pkt@%u temp%.2fC filt%.2fC accel%.3fg\n",
               now_ms, pkt.timestamp, pkt.temp, pkt.filtered, pkt.accel);
        g_tx_tail = (g_tx_tail + 1) % BUF_SIZE;
    }
}

// SCHEDULER
#define NUM_TASKS 4

int main(void) {
    srand((unsigned int)time(NULL));

    task_t tasks[NUM_TASKS] = {
        {"sensor_temp", task_sensor_temp, 100, 0, 5.0f, 1},   // 10 Hz
        {"sensor_accel", task_sensor_accel, 50, 0, 7.0f, 1},  // 20 Hz
        {"background", task_background, 200, 0, 3.0f, 1},     // 5 Hz
        {"comm_uart", task_comm, 500, 0, 10.0f, 1}            // 2 Hz
    };

    const uint32_t SIM_DURATION_MS = 10000;  // 10 seconds

    for (now_ms = 0; now_ms < SIM_DURATION_MS; now_ms += TASK_EXEC_TIME_MS) {
        g_tick_count++;
        uint32_t idle_start = now_ms;
        int any_ran = 0;

        for (int i = 0; i < NUM_TASKS; i++) {
            task_t* t = &tasks[i];
            if (!t->enabled) continue;
            if (now_ms >= t->next_release_ms) {
                uint32_t release_time = t->next_release_ms;
                t->run();  // Run task

                // Timing: response = now - release (includes queue delay)
                uint32_t response_us = (now_ms - release_time) * 1000;
                g_task_response_total[i] += response_us;

                // Simulated exec: always TASK_EXEC_TIME_MS, but check WCET
                uint32_t exec_us = (uint32_t)(TASK_EXEC_TIME_MS * 1000);
                g_task_exec_total[i] += exec_us;
                if (exec_us > WCET_US[i]) g_task_misses[i]++;
                g_task_releases[i]++;

                // Jitter: variance of response time
                static uint32_t last_resp[4] = {0};
                int32_t jitter_us = (int32_t)response_us - (int32_t)last_resp[i];
                g_task_jitter_sq[i] += jitter_us * jitter_us;
                last_resp[i] = response_us;

                g_total_energy_mj += (double)t->power_mw * TASK_EXEC_TIME_MS / 1000.0f;
                t->next_release_ms += t->period_ms;
                any_ran = 1;
            }
        }
        if (!any_ran) g_cpu_idle_ms += TASK_EXEC_TIME_MS;
    }

    double sim_time_s = SIM_DURATION_MS / 1000.0;
    double total_energy_j = g_total_energy_mj / 1000.0;
    double avg_power_mw = g_total_energy_mj / SIM_DURATION_MS * 1000.0;

    printf("\nSimulation summary:\n");
    printf("Simulated time: %.2f s\n", sim_time_s);
    printf("Total energy: %.4f J (%.2f mJ)\n", total_energy_j, g_total_energy_mj);
    printf("Average power: %.2f mW\n", avg_power_mw);

    printf("\n=== Timing Analysis ===\n");
    float cpu_util_pct = 100.0f * (1.0f - (float)g_cpu_idle_ms / SIM_DURATION_MS);
    printf("CPU utilization: %.1f%% (idle %.1f%%)\n", cpu_util_pct, 100-cpu_util_pct);
    for (int i = 0; i < NUM_TASKS; i++) {
        if (g_task_releases[i] == 0) continue;
        float avg_exec_us = (float)g_task_exec_total[i] / g_task_releases[i];
        float avg_resp_us = (float)g_task_response_total[i] / g_task_releases[i];
        float jitter_std_us = sqrtf((float)g_task_jitter_sq[i] / g_task_releases[i]);
        float miss_rate = 100.0f * g_task_misses[i] / g_task_releases[i];
        printf("%s: avg exec %.0fµs, resp %.0fµs, jitter σ=%.0fµs, misses %.1f%%\n",
               tasks[i].name, avg_exec_us, avg_resp_us, jitter_std_us, miss_rate);
    }
    printf("TX buffer: drops=%u, util=%.0f%%\n", g_tx_drops,
       100.0f * ((float)((g_tx_head - g_tx_tail + BUF_SIZE) % BUF_SIZE) / BUF_SIZE));


    return 0;
}
