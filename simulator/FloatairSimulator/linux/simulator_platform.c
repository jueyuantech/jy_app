#include "../simulator_platform.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

void simulator_platform_init_text_io(void)
{
}

int simulator_platform_socket_global_init(void)
{
    return 0;
}

int simulator_platform_socket_open(const simulator_socket_endpoint_t* endpoint, unsigned int timeout_ms, int* out_sock)
{
    const struct sockaddr* address = NULL;
    int socket_fd = -1;
    int flags = 0;
    int result = -1;

    if (!endpoint || !out_sock) {
        return -1;
    }

    address = (const struct sockaddr*)endpoint->storage;
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    }

    result = connect(socket_fd, address, (socklen_t)endpoint->length);
    if (result < 0 && errno == EINPROGRESS) {
        fd_set wset;
        struct timeval timeout = {0};

        FD_ZERO(&wset);
        FD_SET(socket_fd, &wset);
        timeout.tv_sec = (long)(timeout_ms / 1000U);
        timeout.tv_usec = (long)((timeout_ms % 1000U) * 1000U);

        result = select(socket_fd + 1, NULL, &wset, NULL, &timeout);
        if (result > 0) {
            int socket_error = 0;
            socklen_t socket_error_len = (socklen_t)sizeof(socket_error);
            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) == 0 &&
                socket_error == 0) {
                result = 0;
            } else {
                result = -1;
            }
        } else {
            result = -1;
        }
    }

    if (flags >= 0) {
        (void)fcntl(socket_fd, F_SETFL, flags);
    }

    if (result != 0) {
        close(socket_fd);
        return -1;
    }

    *out_sock = socket_fd;
    return 0;
}

int simulator_platform_socket_wait_readable(int sock, int timeout_ms)
{
    fd_set rset;
    struct timeval timeout = {0};
    struct timeval* timeout_ptr = NULL;

    FD_ZERO(&rset);
    FD_SET(sock, &rset);

    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }

    return select(sock + 1, &rset, NULL, NULL, timeout_ptr);
}

int simulator_platform_socket_send(int sock, const void* buf, size_t len)
{
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    return send(sock, (const char*)buf, len, flags);
}

int simulator_platform_socket_recv(int sock, void* buf, size_t len)
{
    return recv(sock, (char*)buf, len, 0);
}

void simulator_platform_socket_close(int* fd)
{
    if (fd && *fd != -1) {
        close(*fd);
        *fd = -1;
    }
}

void simulator_platform_sleep_ms(unsigned int timeout_ms)
{
    usleep(timeout_ms * 1000U);
}

void simulator_platform_get_executable_dir(char* out, size_t outsz)
{
    char* dir = NULL;

    if (!out || outsz == 0) {
        return;
    }

    memset(out, 0, outsz);

#if defined(__APPLE__)
    {
        uint32_t len = (uint32_t)outsz;
        if (_NSGetExecutablePath(out, &len) != 0) {
            out[0] = '.';
            out[1] = '\0';
            return;
        }
    }
#else
    {
        ssize_t len = 0;

        len = readlink("/proc/self/exe", out, outsz - 1U);
        if (len == -1) {
            out[0] = '.';
            out[1] = '\0';
            return;
        }

        out[len] = '\0';
    }
#endif

    dir = dirname(out);
    if (dir != out) {
        memmove(out, dir, strlen(dir) + 1U);
    }
}

void simulator_platform_get_log_time(char* out_time, size_t outsz, long* out_msec)
{
    struct timeval tv;
    time_t now;
    struct tm* tm_info = NULL;

    if (out_time && outsz > 0) {
        out_time[0] = '\0';
    }
    if (out_msec) {
        *out_msec = 0;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        return;
    }

    now = (time_t)tv.tv_sec;
    tm_info = localtime(&now);
    if (tm_info && out_time && outsz > 0) {
        strftime(out_time, outsz, "%H:%M:%S", tm_info);
    }
    if (out_msec) {
        *out_msec = (long)(tv.tv_usec / 1000);
    }
}

FILE* simulator_platform_file_open(const char* path, const char* mode)
{
    return fopen(path, mode);
}

int simulator_platform_stat_path(const char* path, simulator_platform_stat_t* st_out)
{
    struct stat st = {0};

    if (!path || !st_out) {
        return -1;
    }

    if (stat(path, &st) != 0) {
        return -1;
    }

    st_out->is_dir = S_ISDIR(st.st_mode);
    st_out->size = st.st_size > UINT32_MAX ? UINT32_MAX : (uint32_t)st.st_size;
    return 0;
}

int simulator_platform_path_exists(const char* path)
{
    return (path && access(path, F_OK) == 0) ? 0 : -1;
}

int simulator_platform_remove_path(const char* path)
{
    struct stat st = {0};

    if (!path) {
        return -1;
    }

    if (stat(path, &st) != 0) {
        return errno == ENOENT ? 1 : -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return rmdir(path) == 0 ? 0 : -1;
    }
    return unlink(path) == 0 ? 0 : -1;
}

int simulator_platform_rename_path(const char* old_path, const char* new_path)
{
    return rename(old_path, new_path);
}

int simulator_platform_mkdir_one(const char* path)
{
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

int simulator_platform_rmdir(const char* path)
{
    if (!path) {
        return -1;
    }
    return rmdir(path);
}

const char* simulator_platform_fifo_default_path(void)
{
    return "/tmp/floatair_sim_event_fifo";
}

bool simulator_platform_fifo_start(const char* path,
                                   int* fd,
                                   pthread_t* thread,
                                   int* running,
                                   void* (*thread_main)(void*))
{
    if (!path || !fd || !thread || !running || !thread_main) {
        return false;
    }

    if (mkfifo(path, 0666) != 0 && errno != EEXIST) {
        return false;
    }

    *fd = open(path, O_RDWR | O_NONBLOCK);
    if (*fd < 0) {
        return false;
    }

    *running = 1;
    if (pthread_create(thread, NULL, thread_main, NULL) != 0) {
        close(*fd);
        *fd = -1;
        *running = 0;
        return false;
    }

    return true;
}

int simulator_platform_fifo_read(int fd, void* buf, size_t len)
{
    return (int)read(fd, buf, len);
}

bool simulator_platform_fifo_read_would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void simulator_platform_fifo_stop(const char* path,
                                  int* fd,
                                  pthread_t* thread,
                                  int* running)
{
    if (!fd || !thread || !running) {
        return;
    }

    *running = 0;
    pthread_join(*thread, NULL);

    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }

    if (path) {
        unlink(path);
    }
}
