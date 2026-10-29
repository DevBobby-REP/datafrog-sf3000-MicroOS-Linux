#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "input.h"

static FILE* logf = NULL;

static void input_log(const char* fmt, ...)
{
    if (!logf) return;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    fprintf(logf, "[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(logf, fmt, args);
    va_end(args);
    fprintf(logf, "\n");
    fflush(logf);
}

static volatile uint32_t* joy_key_shm = NULL;
static int shmid = -1;

static pthread_t input_thread;
static volatile int thread_running = 0;

static volatile uint32_t current_keys = 0;
static uint8_t debounce_cnt[32] = { 0 };
static uint32_t stable_state = 0;

static int attach_shared_memory(void)
{
    if (joy_key_shm && shmid >= 0)
        return 0;

    int elapsed = 0;
    while (elapsed < 3000) {
        if (access("/tmp/joy_key", F_OK) == 0)
            break;
        usleep(50000);
        elapsed += 50;
    }

    if (elapsed >= 3000) {
        input_log("attach_shared_memory: /tmp/joy_key not found (is cubevol running?)");
        return -1;
    }

    key_t key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) {
        input_log("ftok failed: %s", strerror(errno));
        return -1;
    }

    shmid = shmget(key, 4, IPC_CREAT | 0666);
    if (shmid < 0) {
        input_log("shmget failed: %s", strerror(errno));
        return -1;
    }

    joy_key_shm = (volatile uint32_t*)shmat(shmid, NULL, 0);
    if (joy_key_shm == (volatile uint32_t*)-1) {
        input_log("shmat failed: %s", strerror(errno));
        joy_key_shm = NULL;
        shmid = -1;
        return -1;
    }

    input_log("SHM attached, joy_key_shm=%p, initial=0x%08X",
        (void*)joy_key_shm, *joy_key_shm);
    return 0;
}

static void detach_shared_memory(void)
{
    if (joy_key_shm) {
        shmdt((const void*)joy_key_shm);
        joy_key_shm = NULL;
    }
    shmid = -1;
}

static void debounce_update(uint32_t raw)
{
    for (int b = 0; b < 32; b++) {
        uint32_t mask = 1u << b;
        if ((raw & mask) != (stable_state & mask)) {
            if (++debounce_cnt[b] >= 3) {
                stable_state ^= mask;
                debounce_cnt[b] = 0;
            }
        }
        else {
            debounce_cnt[b] = 0;
        }
    }
    current_keys = stable_state & 0xFFFF;
}

static void* input_poll_thread(void* arg)
{
    input_log("input poll thread started");

    while (thread_running) {
        if (!joy_key_shm) {
            usleep(8000);
            continue;
        }

        uint32_t raw = *joy_key_shm;
        debounce_update(raw);

        usleep(8000); 
    }

    input_log("input poll thread stopped");
    return NULL;
}

int input_init(void)
{
    logf = fopen("/mnt/sdcard/cubegm/MicroOS-Linux/Logs/input.log", "a");
    if (!logf) {
        fprintf(stderr, "input_init: cannot open log file\n");
    }
    else {
        input_log("=== input_init called ===");
    }

    if (joy_key_shm && thread_running)
        return 0;

    if (attach_shared_memory() < 0)
        return -1;

    memset(debounce_cnt, 0, sizeof(debounce_cnt));
    stable_state = 0;
    current_keys = 0;

    thread_running = 1;
    if (pthread_create(&input_thread, NULL, input_poll_thread, NULL) != 0) {
        input_log("pthread_create failed");
        thread_running = 0;
        detach_shared_memory();
        return -1;
    }

    usleep(20000);
    input_log("input_init: success, current_keys=0x%04X", current_keys);
    return 0;
}

uint32_t input_raw(void)
{
    return current_keys;
}

int input_pressed(uint32_t button)
{
    return (current_keys & button) != 0;
}

int input_check_health(void)
{
    if (!thread_running || !joy_key_shm)
        return 0;
    return 1;
}

int input_reinit(void)
{
    input_log("=== input_reinit called ===");

    if (thread_running) {
        thread_running = 0;
        pthread_join(input_thread, NULL);
    }

    detach_shared_memory();

    memset(debounce_cnt, 0, sizeof(debounce_cnt));
    stable_state = 0;
    current_keys = 0;

    if (attach_shared_memory() < 0)
        return -1;

    thread_running = 1;
    if (pthread_create(&input_thread, NULL, input_poll_thread, NULL) != 0) {
        input_log("pthread_create failed in reinit");
        thread_running = 0;
        return -1;
    }

    input_log("input_reinit: success");
    return 0;
}

void input_shutdown(void)
{
    if (thread_running) {
        thread_running = 0;
        pthread_join(input_thread, NULL);
    }
    detach_shared_memory();

    if (logf) {
        input_log("=== input_shutdown called ===");
        fclose(logf);
        logf = NULL;
    }
}