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
}

static void task_sensor_accel(void) {
    float noise = randf(-0.2f, 0.2f);
    g_accel = fminf(fmaxf(sin(now_ms * 0.001f) + noise, -2.0f), 2.0f);  // ±2g
    printf("%6u ms ACCEL: Accel reading %.3f g\n", now_ms, g_accel);
}


static void task_background(void) {
    // Simple exponential moving average filter
    const float alpha = 0.1f;
    g_temp_filtered = alpha * g_temp_c + (1.0f - alpha) * g_temp_filtered;
    printf("%6u ms BG: Filtered temp %.2f C\n", now_ms, g_temp_filtered);
}

static void task_comm(void) {
    // Send latest values over "UART" (here: print one line)
    printf("%6u ms COMM: TX temp%.2fC, filt%.2fC, accel%.3fg\n",
           now_ms, g_temp_c, g_temp_filtered, g_accel);
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
        for (int i = 0; i < NUM_TASKS; i++) {
            task_t* t = &tasks[i];
            if (!t->enabled) continue;
            if (now_ms >= t->next_release_ms) {
                // Run the task once
                t->run();

                // Energy accounting: P(mW) * t(ms) / 1000 = mJ
                g_total_energy_mj += (double)t->power_mw * TASK_EXEC_TIME_MS / 1000.0f;

                // Schedule next release
                t->next_release_ms += t->period_ms;
            }
        }
    }

    double sim_time_s = SIM_DURATION_MS / 1000.0;
    double total_energy_j = g_total_energy_mj / 1000.0;
    double avg_power_mw = g_total_energy_mj / SIM_DURATION_MS * 1000.0;

    printf("\nSimulation summary:\n");
    printf("Simulated time: %.2f s\n", sim_time_s);
    printf("Total energy: %.4f J (%.2f mJ)\n", total_energy_j, g_total_energy_mj);
    printf("Average power: %.2f mW\n", avg_power_mw);

    return 0;
}

