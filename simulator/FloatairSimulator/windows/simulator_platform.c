#include "../simulator_platform.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dirent.h>
#include <direct.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

#if defined(_MSC_VER) && !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

static int g_winsock_initialized = 0;
static int g_fifo_pipe_connected = 0;
static HANDLE g_fifo_pipe = INVALID_HANDLE_VALUE;

static bool simulator_platform_utf8_to_wide(const char* src, wchar_t* dst, size_t dst_count)
{
    int len = 0;

    if (!src || !dst || dst_count == 0) {
        errno = EINVAL;
        return false;
    }

    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, (int)dst_count);
    if (len <= 0) {
        errno = EILSEQ;
        dst[0] = L'\0';
        return false;
    }
    return true;
}

void simulator_platform_init_text_io(void)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

int simulator_platform_socket_global_init(void)
{
    WSADATA wsa_data;

    if (g_winsock_initialized) {
        return 0;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return -1;
    }

    g_winsock_initialized = 1;
    return 0;
}

int simulator_platform_socket_open(const simulator_socket_endpoint_t* endpoint, unsigned int timeout_ms, int* out_sock)
{
    const struct sockaddr* address = NULL;
    SOCKET socket_fd = INVALID_SOCKET;
    int result = -1;
    u_long nonblocking = 1;
    u_long blocking_mode = 0;

    (void)timeout_ms;

    if (!endpoint || !out_sock) {
        return -1;
    }

    address = (const struct sockaddr*)endpoint->storage;
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == INVALID_SOCKET) {
        return -1;
    }

    ioctlsocket(socket_fd, FIONBIO, &nonblocking);
    result = connect(socket_fd, address, endpoint->length);
    if (result == SOCKET_ERROR) {
        int wsa_error = WSAGetLastError();
        if (wsa_error == WSAEWOULDBLOCK || wsa_error == WSAEINPROGRESS || wsa_error == WSAEINVAL) {
            fd_set wset;
            struct timeval timeout = {0};

            FD_ZERO(&wset);
            FD_SET(socket_fd, &wset);
            timeout.tv_sec = (long)(timeout_ms / 1000U);
            timeout.tv_usec = (long)((timeout_ms % 1000U) * 1000U);

            result = select((int)socket_fd + 1, NULL, &wset, NULL, &timeout);
            if (result > 0) {
                int socket_error = 0;
                int socket_error_len = (int)sizeof(socket_error);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (char*)&socket_error, &socket_error_len) == 0 &&
                    socket_error == 0) {
                    result = 0;
                } else {
                    result = -1;
                }
            } else {
                result = -1;
            }
        }
    }

    ioctlsocket(socket_fd, FIONBIO, &blocking_mode);

    if (result != 0) {
        closesocket(socket_fd);
        return -1;
    }

    *out_sock = (int)socket_fd;
    return 0;
}

int simulator_platform_socket_wait_readable(int sock, int timeout_ms)
{
    fd_set rset;
    struct timeval timeout = {0};
    struct timeval* timeout_ptr = NULL;

    FD_ZERO(&rset);
    FD_SET((SOCKET)sock, &rset);

    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }

    return select(sock + 1, &rset, NULL, NULL, timeout_ptr);
}

int simulator_platform_socket_send(int sock, const void* buf, size_t len)
{
    return send((SOCKET)sock, (const char*)buf, (int)len, 0);
}

int simulator_platform_socket_recv(int sock, void* buf, size_t len)
{
    return recv((SOCKET)sock, (char*)buf, (int)len, 0);
}

void simulator_platform_socket_close(int* fd)
{
    if (fd && *fd != -1) {
        closesocket((SOCKET)*fd);
        *fd = -1;
    }
}

void simulator_platform_sleep_ms(unsigned int timeout_ms)
{
    Sleep(timeout_ms);
}

void simulator_platform_get_executable_dir(char* out, size_t outsz)
{
    DWORD len = 0;
    char* slash = NULL;

    if (!out || outsz == 0) {
        return;
    }

    memset(out, 0, outsz);
    len = GetModuleFileNameA(NULL, out, (DWORD)outsz);
    if (len == 0 || len >= outsz) {
        out[0] = '.';
        out[1] = '\0';
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        if (out[i] == '\\') {
            out[i] = '/';
        }
    }

    slash = strrchr(out, '/');
    if (slash) {
        *slash = '\0';
    } else {
        out[0] = '.';
        out[1] = '\0';
    }
}

void simulator_platform_get_log_time(char* out_time, size_t outsz, long* out_msec)
{
    SYSTEMTIME now;

    if (out_time && outsz > 0) {
        out_time[0] = '\0';
    }
    if (out_msec) {
        *out_msec = 0;
    }

    GetLocalTime(&now);
    if (out_time && outsz > 0) {
        snprintf(out_time, outsz, "%02u:%02u:%02u",
                 (unsigned)now.wHour,
                 (unsigned)now.wMinute,
                 (unsigned)now.wSecond);
    }
    if (out_msec) {
        *out_msec = (long)now.wMilliseconds;
    }
}

FILE* simulator_platform_file_open(const char* path, const char* mode)
{
    wchar_t wpath[1024] = {0};
    wchar_t wmode[16] = {0};

    if (!simulator_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0])) ||
        !simulator_platform_utf8_to_wide(mode, wmode, sizeof(wmode) / sizeof(wmode[0]))) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
}

int simulator_platform_stat_path(const char* path, simulator_platform_stat_t* st_out)
{
    wchar_t wpath[1024] = {0};
    struct __stat64 st = {0};

    if (!path || !st_out) {
        errno = EINVAL;
        return -1;
    }

    if (!simulator_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) {
        return -1;
    }
    if (_wstat64(wpath, &st) != 0) {
        return -1;
    }

    st_out->is_dir = S_ISDIR(st.st_mode);
    st_out->size = st.st_size > UINT32_MAX ? UINT32_MAX : (uint32_t)st.st_size;
    return 0;
}

int simulator_platform_path_exists(const char* path)
{
    simulator_platform_stat_t st = {0};
    return (path && simulator_platform_stat_path(path, &st) == 0) ? 0 : -1;
}

int simulator_platform_remove_path(const char* path)
{
    wchar_t wpath[1024] = {0};
    simulator_platform_stat_t st = {0};

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (simulator_platform_stat_path(path, &st) != 0) {
        return errno == ENOENT ? 1 : -1;
    }

    if (!simulator_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) {
        return -1;
    }
    if (st.is_dir) {
        return _wrmdir(wpath) == 0 ? 0 : -1;
    }
    return _wremove(wpath) == 0 ? 0 : -1;
}

int simulator_platform_rename_path(const char* old_path, const char* new_path)
{
    wchar_t old_wpath[1024] = {0};
    wchar_t new_wpath[1024] = {0};

    if (!simulator_platform_utf8_to_wide(old_path, old_wpath, sizeof(old_wpath) / sizeof(old_wpath[0])) ||
        !simulator_platform_utf8_to_wide(new_path, new_wpath, sizeof(new_wpath) / sizeof(new_wpath[0]))) {
        return -1;
    }
    return _wrename(old_wpath, new_wpath);
}

int simulator_platform_mkdir_one(const char* path)
{
    wchar_t wpath[1024] = {0};

    if (!simulator_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) {
        return -1;
    }
    if (_wmkdir(wpath) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

int simulator_platform_rmdir(const char* path)
{
    wchar_t wpath[1024] = {0};

    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (!simulator_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) {
        return -1;
    }
    return _wrmdir(wpath);
}

const char* simulator_platform_fifo_default_path(void)
{
    return "\\\\.\\pipe\\floatair_sim_event_fifo";
}

bool simulator_platform_fifo_start(const char* path,
                                   int* fd,
                                   pthread_t* thread,
                                   int* running,
                                   void* (*thread_main)(void*))
{
    HANDLE pipe = INVALID_HANDLE_VALUE;

    if (!path || !fd || !thread || !running || !thread_main) {
        return false;
    }

    pipe = CreateNamedPipeA(path,
                            PIPE_ACCESS_INBOUND,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                            1,
                            4096,
                            4096,
                            0,
                            NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        *fd = -1;
        return false;
    }

    g_fifo_pipe_connected = 0;
    g_fifo_pipe = pipe;
    *fd = 0;
    *running = 1;
    if (pthread_create(thread, NULL, thread_main, NULL) != 0) {
        CloseHandle(pipe);
        g_fifo_pipe = INVALID_HANDLE_VALUE;
        *fd = -1;
        *running = 0;
        return false;
    }

    return true;
}

int simulator_platform_fifo_read(int fd, void* buf, size_t len)
{
    HANDLE pipe = g_fifo_pipe;
    DWORD bytes_read = 0;

    if (fd < 0 || pipe == INVALID_HANDLE_VALUE || !buf || len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (!g_fifo_pipe_connected) {
        BOOL connected = ConnectNamedPipe(pipe, NULL);
        DWORD err = GetLastError();

        if (connected || err == ERROR_PIPE_CONNECTED) {
            g_fifo_pipe_connected = 1;
        } else if (err == ERROR_OPERATION_ABORTED || err == ERROR_INVALID_HANDLE) {
            errno = EAGAIN;
            return -1;
        } else {
            errno = EIO;
            return -1;
        }
    }

    if (!ReadFile(pipe, buf, (DWORD)len, &bytes_read, NULL)) {
        DWORD err = GetLastError();

        if (err == ERROR_NO_DATA || err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED ||
            err == ERROR_INVALID_HANDLE) {
            DisconnectNamedPipe(pipe);
            g_fifo_pipe_connected = 0;
            errno = EAGAIN;
            return -1;
        }
        errno = EIO;
        return -1;
    }

    return (int)bytes_read;
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
    (void)path;

    if (!fd || !thread || !running) {
        return;
    }

    *running = 0;

    if (*fd != -1) {
        HANDLE pipe = g_fifo_pipe;
        CloseHandle(pipe);
        g_fifo_pipe = INVALID_HANDLE_VALUE;
        *fd = -1;
    }

    pthread_join(*thread, NULL);

    g_fifo_pipe_connected = 0;
}
