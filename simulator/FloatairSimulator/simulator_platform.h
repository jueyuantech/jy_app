#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef struct {
    unsigned char storage[128];
    int length;
} simulator_socket_endpoint_t;

typedef struct {
    bool is_dir;     /* 路径是否为目录。 */
    uint32_t size;   /* 文件大小，超过 32 位时截断为 UINT32_MAX。 */
} simulator_platform_stat_t;

int simulator_platform_socket_global_init(void);
int simulator_platform_socket_open(const simulator_socket_endpoint_t* endpoint, unsigned int timeout_ms, int* out_sock);
int simulator_platform_socket_wait_readable(int sock, int timeout_ms);
int simulator_platform_socket_send(int sock, const void* buf, size_t len);
int simulator_platform_socket_recv(int sock, void* buf, size_t len);
void simulator_platform_socket_close(int* fd);

void simulator_platform_init_text_io(void);
void simulator_platform_sleep_ms(unsigned int timeout_ms);
void simulator_platform_get_executable_dir(char* out, size_t outsz);
void simulator_platform_get_log_time(char* out_time, size_t outsz, long* out_msec);
FILE* simulator_platform_file_open(const char* path, const char* mode);
int simulator_platform_stat_path(const char* path, simulator_platform_stat_t* st_out);
int simulator_platform_path_exists(const char* path);
int simulator_platform_remove_path(const char* path);
int simulator_platform_rename_path(const char* old_path, const char* new_path);
int simulator_platform_mkdir_one(const char* path);
int simulator_platform_rmdir(const char* path);

const char* simulator_platform_fifo_default_path(void);
bool simulator_platform_fifo_start(const char* path,
                                   int* fd,
                                   pthread_t* thread,
                                   int* running,
                                   void* (*thread_main)(void*));
int simulator_platform_fifo_read(int fd, void* buf, size_t len);
bool simulator_platform_fifo_read_would_block(void);
void simulator_platform_fifo_stop(const char* path,
                                  int* fd,
                                  pthread_t* thread,
                                  int* running);
