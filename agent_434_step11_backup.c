#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2434"
#define SID "4342"

#define BUFFER_SIZE 1024
#define STORAGE_DIR "./agentfiles/IT24102434"
#define MAX_FILE_SIZE (10 * 1024 * 1024)

#define MONITOR_INTERVAL 5

typedef struct
{
    int active;
    int udp_port;
    char client_ip[INET_ADDRSTRLEN];
    pthread_t thread_id;

} MonitorContext;


// Receive one complete TCP line ending with \n
int recv_line(int sock, char *buffer, int size)
{
    int index = 0;
    char ch;
    int received;

    while (index < size - 1)
    {
        received = recv(sock, &ch, 1, 0);

        if (received == 0)
            return 0;

        if (received < 0)
            return -1;

        if (ch == '\n')
            break;

        buffer[index++] = ch;
    }

    buffer[index] = '\0';

    return index;
}


// Send all text bytes
int send_all(int sock, const char *message)
{
    int total_sent = 0;
    int length = strlen(message);

    while (total_sent < length)
    {
        int sent = send(sock,
                        message + total_sent,
                        length - total_sent,
                        0);

        if (sent <= 0)
            return -1;

        total_sent += sent;
    }

    return 0;
}


// Get current Linux system statistics
int get_system_stats(double *cpu_load,
                     unsigned long long *mem_used_mb,
                     long *uptime_sec)
{
    struct sysinfo info;

    if (sysinfo(&info) != 0)
        return -1;

    *cpu_load =
        (double)info.loads[0] / 65536.0;

    unsigned long long total_ram =
        (unsigned long long)info.totalram *
        info.mem_unit;

    unsigned long long free_ram =
        (unsigned long long)info.freeram *
        info.mem_unit;

    *mem_used_mb =
        (total_ram - free_ram) /
        (1024 * 1024);

    *uptime_sec =
        info.uptime;

    return 0;
}


// SYSINFO command
void send_sysinfo(int client_fd)
{
    char response[256];

    double cpu_load;
    unsigned long long mem_used_mb;
    long uptime_sec;

    if (get_system_stats(&cpu_load,
                         &mem_used_mb,
                         &uptime_sec) != 0)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 006 SYSINFO_FAILED SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    snprintf(response,
             sizeof(response),
             "OK SYSINFO %.2f %llu %ld SID:%s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SID);

    send_all(client_fd, response);
}


// LISTPROC command
void send_listproc(int client_fd)
{
    FILE *fp;
    char line[256];
    char process_list[800] = "";
    char response[1024];

    fp = popen("ps -eo pid,comm --no-headers", "r");

    if (fp == NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 007 LISTPROC_FAILED SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        line[strcspn(line, "\n")] = '\0';

        if (strlen(process_list) +
            strlen(line) + 2 <
            sizeof(process_list))
        {
            strcat(process_list, line);
            strcat(process_list, ",");
        }
        else
        {
            break;
        }
    }

    pclose(fp);

    if (strlen(process_list) > 0)
    {
        process_list[
            strlen(process_list) - 1
        ] = '\0';
    }

    snprintf(response,
             sizeof(response),
             "OK PROCS %s SID:%s\n",
             process_list,
             SID);

    send_all(client_fd, response);
}


// Replace newlines in EXEC output with spaces
void clean_output(char *text)
{
    int i;

    for (i = 0; text[i] != '\0'; i++)
    {
        if (text[i] == '\n' ||
            text[i] == '\r')
        {
            text[i] = ' ';
        }
    }

    while (strlen(text) > 0 &&
           text[strlen(text) - 1] == ' ')
    {
        text[strlen(text) - 1] = '\0';
    }
}


// EXEC whitelist
void handle_exec(int client_fd,
                 const char *name)
{
    const char *linux_command = NULL;

    FILE *fp;
    char line[256];
    char output[700] = "";
    char response[1024];

    if (strcmp(name, "DATE") == 0)
    {
        linux_command = "date";
    }
    else if (strcmp(name, "UPTIME") == 0)
    {
        linux_command = "uptime";
    }
    else if (strcmp(name, "DISKFREE") == 0)
    {
        linux_command = "df -h /";
    }
    else if (strcmp(name, "HOSTNAME") == 0)
    {
        linux_command = "hostname";
    }
    else if (strcmp(name, "WHOAMI") == 0)
    {
        linux_command = "whoami";
    }
    else
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                 SID);

        send_all(client_fd, response);

        printf("EXEC command rejected: %s\n",
               name);

        return;
    }

    fp = popen(linux_command, "r");

    if (fp == NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 008 EXEC_FAILED SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        if (strlen(output) +
            strlen(line) + 1 <
            sizeof(output))
        {
            strcat(output, line);
        }
        else
        {
            break;
        }
    }

    pclose(fp);

    clean_output(output);

    snprintf(response,
             sizeof(response),
             "OK EXEC_RESULT %s SID:%s\n",
             output,
             SID);

    send_all(client_fd, response);

    printf("EXEC command allowed: %s\n",
           name);
}


// Discard raw bytes
int discard_bytes(int client_fd,
                  long long filesize)
{
    char buffer[BUFFER_SIZE];

    long long remaining = filesize;

    while (remaining > 0)
    {
        int amount =
            remaining > BUFFER_SIZE
            ? BUFFER_SIZE
            : (int)remaining;

        int received =
            recv(client_fd,
                 buffer,
                 amount,
                 0);

        if (received <= 0)
            return -1;

        remaining -= received;
    }

    return 0;
}


// PUT file upload
void handle_put(int client_fd,
                const char *filename,
                long long filesize)
{
    char filepath[512];
    char response[512];
    char buffer[BUFFER_SIZE];

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL)
    {
        discard_bytes(client_fd, filesize);

        snprintf(response,
                 sizeof(response),
                 "ERR 009 INVALID_FILENAME SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    if (filesize < 0)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 010 INVALID_FILESIZE SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    if (filesize > MAX_FILE_SIZE)
    {
        discard_bytes(client_fd, filesize);

        snprintf(response,
                 sizeof(response),
                 "ERR 004 FILE_TOO_LARGE SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *fp =
        fopen(filepath, "wb");

    if (fp == NULL)
    {
        discard_bytes(client_fd, filesize);

        snprintf(response,
                 sizeof(response),
                 "ERR 011 FILE_WRITE_FAILED SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    long long remaining = filesize;

    while (remaining > 0)
    {
        int amount =
            remaining > BUFFER_SIZE
            ? BUFFER_SIZE
            : (int)remaining;

        int received =
            recv(client_fd,
                 buffer,
                 amount,
                 0);

        if (received <= 0)
        {
            fclose(fp);

            remove(filepath);

            return;
        }

        size_t written =
            fwrite(buffer,
                   1,
                   received,
                   fp);

        if (written !=
            (size_t)received)
        {
            fclose(fp);

            remove(filepath);

            return;
        }

        remaining -= received;
    }

    fclose(fp);

    snprintf(response,
             sizeof(response),
             "OK FILE_RECEIVED %s SID:%s\n",
             filename,
             SID);

    send_all(client_fd, response);

    printf("File received: %s (%lld bytes)\n",
           filename,
           filesize);
}


// GET file download
void handle_get(int client_fd,
                const char *filename)
{
    char filepath[512];
    char response[512];
    char buffer[BUFFER_SIZE];

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_NOT_FOUND SID:%s\n",
                 SID);

        send_all(client_fd, response);

        return;
    }

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *fp =
        fopen(filepath, "rb");

    if (fp == NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_NOT_FOUND SID:%s\n",
                 SID);

        send_all(client_fd, response);

        printf("GET failed: file not found: %s\n",
               filename);

        return;
    }

    fseek(fp, 0, SEEK_END);

    long long filesize =
        (long long)ftell(fp);

    rewind(fp);

    snprintf(response,
             sizeof(response),
             "OK FILE_SEND %s %lld SID:%s\n",
             filename,
             filesize,
             SID);

    if (send_all(client_fd,
                 response) < 0)
    {
        fclose(fp);

        return;
    }

    long long total_sent = 0;

    while (total_sent < filesize)
    {
        size_t bytes_read =
            fread(buffer,
                  1,
                  sizeof(buffer),
                  fp);

        if (bytes_read == 0)
            break;

        int sent_total = 0;

        while (sent_total <
               (int)bytes_read)
        {
            int sent =
                send(client_fd,
                     buffer + sent_total,
                     bytes_read - sent_total,
                     0);

            if (sent <= 0)
            {
                fclose(fp);

                return;
            }

            sent_total += sent;
        }

        total_sent += bytes_read;
    }

    fclose(fp);

    printf("File sent: %s (%lld bytes)\n",
           filename,
           total_sent);
}


// UDP monitoring thread
void *monitor_thread(void *arg)
{
    MonitorContext *ctx =
        (MonitorContext *)arg;

    int udp_sock =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udp_sock < 0)
    {
        perror("UDP socket creation failed");

        ctx->active = 0;

        return NULL;
    }

    struct sockaddr_in controller_addr;

    memset(&controller_addr,
           0,
           sizeof(controller_addr));

    controller_addr.sin_family =
        AF_INET;

    controller_addr.sin_port =
        htons(ctx->udp_port);

    inet_pton(AF_INET,
              ctx->client_ip,
              &controller_addr.sin_addr);

    while (ctx->active)
    {
        double cpu_load;

        unsigned long long mem_used_mb;

        long uptime_sec;

        if (get_system_stats(
                &cpu_load,
                &mem_used_mb,
                &uptime_sec) == 0)
        {
            char message[256];

            snprintf(message,
                     sizeof(message),
                     "SYSINFO %.2f %llu %ld SID:%s",
                     cpu_load,
                     mem_used_mb,
                     uptime_sec,
                     SID);

            sendto(udp_sock,
                   message,
                   strlen(message),
                   0,
                   (struct sockaddr *)
                   &controller_addr,
                   sizeof(controller_addr));

            printf(
                "UDP monitor sent to %s:%d\n",
                ctx->client_ip,
                ctx->udp_port
            );
        }

        sleep(MONITOR_INTERVAL);
    }

    close(udp_sock);

    return NULL;
}


// Start UDP monitoring
int start_monitoring(MonitorContext *ctx,
                     const char *client_ip,
                     int udp_port)
{
    if (ctx->active)
        return -1;

    ctx->active = 1;

    ctx->udp_port =
        udp_port;

    strncpy(ctx->client_ip,
            client_ip,
            sizeof(ctx->client_ip) - 1);

    ctx->client_ip[
        sizeof(ctx->client_ip) - 1
    ] = '\0';

    if (pthread_create(
            &ctx->thread_id,
            NULL,
            monitor_thread,
            ctx) != 0)
    {
        ctx->active = 0;

        return -1;
    }

    return 0;
}


// Stop UDP monitoring
void stop_monitoring(MonitorContext *ctx)
{
    if (!ctx->active)
        return;

    ctx->active = 0;

    pthread_join(ctx->thread_id,
                 NULL);
}


// Main Agent program
int main()
{
    int server_fd;

    struct sockaddr_in server_addr;

    mkdir("./agentfiles", 0755);

    if (mkdir(STORAGE_DIR, 0755) < 0 &&
        errno != EEXIST)
    {
        perror("Could not create storage directory");

        return 1;
    }

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0)
    {
        perror("Socket creation failed");

        return 1;
    }

    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)
             &server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("Bind failed");

        close(server_fd);

        return 1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");

        close(server_fd);

        return 1;
    }

    printf("RemoteOps Agent starting...\n");

    printf("Agent listening on TCP port %d...\n",
           PORT);

    printf("Storage directory: %s\n",
           STORAGE_DIR);

    printf("UDP monitoring interval: %d seconds\n",
           MONITOR_INTERVAL);

    printf("Waiting for Controller connections...\n");

    while (1)
    {
        int client_fd;

        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        char buffer[BUFFER_SIZE];

        int authenticated = 0;

        MonitorContext monitor;

        memset(&monitor,
               0,
               sizeof(monitor));

        client_fd =
            accept(server_fd,
                   (struct sockaddr *)
                   &client_addr,
                   &client_len);

        if (client_fd < 0)
        {
            perror("Accept failed");

            continue;
        }

        char client_ip[INET_ADDRSTRLEN];

        inet_ntop(AF_INET,
                  &client_addr.sin_addr,
                  client_ip,
                  sizeof(client_ip));

        printf("\nController connected from %s\n",
               client_ip);

        while (1)
        {
            int result =
                recv_line(client_fd,
                          buffer,
                          sizeof(buffer));

            if (result <= 0)
            {
                printf("Controller disconnected.\n");

                break;
            }

            printf("Received: %s\n",
                   buffer);

            // Authentication
            if (!authenticated)
            {
                if (strncmp(buffer,
                            "AUTH ",
                            5) == 0)
                {
                    char *token =
                        buffer + 5;

                    if (strcmp(token,
                               AUTH_TOKEN) == 0)
                    {
                        char response[128];

                        snprintf(response,
                                 sizeof(response),
                                 "OK AUTHENTICATED SID:%s\n",
                                 SID);

                        send_all(client_fd,
                                 response);

                        authenticated = 1;

                        printf(
                            "Authentication successful.\n"
                        );
                    }
                    else
                    {
                        char response[128];

                        snprintf(response,
                                 sizeof(response),
                                 "ERR 001 AUTH_FAILED SID:%s\n",
                                 SID);

                        send_all(client_fd,
                                 response);

                        printf(
                            "Authentication failed.\n"
                        );
                    }
                }
                else
                {
                    char response[128];

                    snprintf(response,
                             sizeof(response),
                             "ERR 003 AUTH_REQUIRED SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response);
                }

                continue;
            }

            // SYSINFO
            if (strcmp(buffer,
                       "SYSINFO") == 0)
            {
                send_sysinfo(client_fd);

                printf("SYSINFO response sent.\n");
            }

            // LISTPROC
            else if (strcmp(buffer,
                            "LISTPROC") == 0)
            {
                send_listproc(client_fd);

                printf("LISTPROC response sent.\n");
            }

            // EXEC
            else if (strncmp(buffer,
                             "EXEC ",
                             5) == 0)
            {
                handle_exec(client_fd,
                            buffer + 5);
            }

            // PUT
            else if (strncmp(buffer,
                             "PUT ",
                             4) == 0)
            {
                char filename[256];

                long long filesize;

                if (sscanf(buffer,
                           "PUT %255s %lld",
                           filename,
                           &filesize) == 2)
                {
                    handle_put(client_fd,
                               filename,
                               filesize);
                }
                else
                {
                    char response[128];

                    snprintf(response,
                             sizeof(response),
                             "ERR 012 INVALID_PUT_FORMAT SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response);
                }
            }

            // GET
            else if (strncmp(buffer,
                             "GET ",
                             4) == 0)
            {
                char filename[256];

                if (sscanf(buffer,
                           "GET %255s",
                           filename) == 1)
                {
                    handle_get(client_fd,
                               filename);
                }
                else
                {
                    char response[128];

                    snprintf(response,
                             sizeof(response),
                             "ERR 013 INVALID_GET_FORMAT SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response);
                }
            }

            // MONITOR START
            else if (strncmp(buffer,
                             "MONITOR START ",
                             14) == 0)
            {
                int udp_port;

                if (sscanf(buffer,
                           "MONITOR START %d",
                           &udp_port) == 1 &&
                    udp_port > 0 &&
                    udp_port <= 65535)
                {
                    char response[128];

                    if (start_monitoring(
                            &monitor,
                            client_ip,
                            udp_port) == 0)
                    {
                        snprintf(response,
                                 sizeof(response),
                                 "OK MONITOR_STARTED SID:%s\n",
                                 SID);

                        send_all(client_fd,
                                 response);

                        printf(
                            "UDP monitoring started on port %d.\n",
                            udp_port
                        );
                    }
                    else
                    {
                        snprintf(response,
                                 sizeof(response),
                                 "ERR 014 MONITOR_ALREADY_RUNNING SID:%s\n",
                                 SID);

                        send_all(client_fd,
                                 response);
                    }
                }
                else
                {
                    char response[128];

                    snprintf(response,
                             sizeof(response),
                             "ERR 015 INVALID_UDP_PORT SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response);
                }
            }

            // MONITOR STOP
            else if (strcmp(buffer,
                            "MONITOR STOP") == 0)
            {
                stop_monitoring(&monitor);

                char response[128];

                snprintf(response,
                         sizeof(response),
                         "OK MONITOR_STOPPED SID:%s\n",
                         SID);

                send_all(client_fd,
                         response);

                printf("UDP monitoring stopped.\n");
            }

            // QUIT
            else if (strcmp(buffer,
                            "QUIT") == 0)
            {
                stop_monitoring(&monitor);

                char response[128];

                snprintf(response,
                         sizeof(response),
                         "OK BYE SID:%s\n",
                         SID);

                send_all(client_fd,
                         response);

                printf("Controller requested QUIT.\n");

                break;
            }

            // Unknown command
            else
            {
                char response[128];

                snprintf(response,
                         sizeof(response),
                         "ERR 099 UNKNOWN_COMMAND SID:%s\n",
                         SID);

                send_all(client_fd,
                         response);
            }
        }

        stop_monitoring(&monitor);

        close(client_fd);

        printf("Controller connection closed.\n");
    }

    close(server_fd);

    return 0;
}
