#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2434"
#define SID "4342"
#define BUFFER_SIZE 1024

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

void send_sysinfo(int client_fd)
{
    struct sysinfo info;
    char response[256];

    if (sysinfo(&info) != 0)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 006 SYSINFO_FAILED SID:%s\n",
                 SID);

        send_all(client_fd, response);
        return;
    }

    double cpu_load =
        (double)info.loads[0] / 65536.0;

    long mem_used_mb =
        (info.totalram - info.freeram) /
        (1024 * 1024);

    long uptime_sec =
        info.uptime;

    snprintf(response,
             sizeof(response),
             "OK SYSINFO %.2f %ld %ld SID:%s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SID);

    send_all(client_fd, response);
}

int main()
{
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

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

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
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
    printf("Agent listening on TCP port %d...\n", PORT);
    printf("Waiting for Controller connections...\n");

    while (1)
    {
        int client_fd;
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        char buffer[BUFFER_SIZE];
        int authenticated = 0;

        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_len);

        if (client_fd < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("\nController connected from %s\n",
               inet_ntoa(client_addr.sin_addr));

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

            printf("Received: %s\n", buffer);

            if (!authenticated)
            {
                if (strncmp(buffer, "AUTH ", 5) == 0)
                {
                    char *token = buffer + 5;

                    if (strcmp(token, AUTH_TOKEN) == 0)
                    {
                        char response[128];

                        snprintf(response,
                                 sizeof(response),
                                 "OK AUTHENTICATED SID:%s\n",
                                 SID);

                        send_all(client_fd, response);

                        authenticated = 1;

                        printf("Authentication successful.\n");
                    }
                    else
                    {
                        char response[128];

                        snprintf(response,
                                 sizeof(response),
                                 "ERR 001 AUTH_FAILED SID:%s\n",
                                 SID);

                        send_all(client_fd, response);

                        printf("Authentication failed.\n");
                    }
                }
                else
                {
                    char response[128];

                    snprintf(response,
                             sizeof(response),
                             "ERR 003 AUTH_REQUIRED SID:%s\n",
                             SID);

                    send_all(client_fd, response);
                }

                continue;
            }

            if (strcmp(buffer, "SYSINFO") == 0)
            {
                send_sysinfo(client_fd);

                printf("SYSINFO response sent.\n");
            }
            else if (strcmp(buffer, "QUIT") == 0)
            {
                char response[128];

                snprintf(response,
                         sizeof(response),
                         "OK BYE SID:%s\n",
                         SID);

                send_all(client_fd, response);

                printf("Controller requested QUIT.\n");
                break;
            }
            else
            {
                char response[128];

                snprintf(response,
                         sizeof(response),
                         "ERR 099 UNKNOWN_COMMAND SID:%s\n",
                         SID);

                send_all(client_fd, response);
            }
        }

        close(client_fd);

        printf("Controller connection closed.\n");
    }

    close(server_fd);

    return 0;
}
