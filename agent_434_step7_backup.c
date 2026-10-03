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

// Receive one complete text line ending with '\n'
int recv_line(int sock, char *buffer, int size)
{
    int index = 0;
    char ch;
    int received;

    while (index < size - 1)
    {
        received = recv(sock, &ch, 1, 0);

        if (received == 0)
        {
            return 0;
        }

        if (received < 0)
        {
            return -1;
        }

        if (ch == '\n')
        {
            break;
        }

        buffer[index++] = ch;
    }

    buffer[index] = '\0';

    return index;
}

// Send all bytes of a text response
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
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

// SYSINFO handler
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

// LISTPROC handler
void send_listproc(int client_fd)
{
    FILE *fp;
    char line[256];
    char process_list[800] = "";
    char response[1024];

    // Run Linux ps command
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

    // Read process list line by line
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        // Remove newline
        line[strcspn(line, "\n")] = '\0';

        // Avoid overflowing process_list
        if (strlen(process_list) + strlen(line) + 2
            < sizeof(process_list))
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

    // Remove final comma
    if (strlen(process_list) > 0)
    {
        process_list[strlen(process_list) - 1] = '\0';
    }

    snprintf(response,
             sizeof(response),
             "OK PROCS %s SID:%s\n",
             process_list,
             SID);

    send_all(client_fd, response);
}

// Main Agent
int main()
{
    int server_fd;
    struct sockaddr_in server_addr;

    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    // Allow quick port reuse
    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind Agent to port 9410
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // Listen for incoming Controllers
    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent starting...\n");
    printf("Agent listening on TCP port %d...\n", PORT);
    printf("Waiting for Controller connections...\n");

    // Keep Agent running
    while (1)
    {
        int client_fd;
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        char buffer[BUFFER_SIZE];

        // 0 = not authenticated
        // 1 = authenticated
        int authenticated = 0;

        // Accept Controller connection
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

        // Handle commands from this Controller
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

            // Authentication stage
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

                    printf("Command rejected: authentication required.\n");
                }

                continue;
            }

            // SYSINFO command
            if (strcmp(buffer, "SYSINFO") == 0)
            {
                send_sysinfo(client_fd);

                printf("SYSINFO response sent.\n");
            }

            // LISTPROC command
            else if (strcmp(buffer, "LISTPROC") == 0)
            {
                send_listproc(client_fd);

                printf("LISTPROC response sent.\n");
            }

            // QUIT command
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

            // Unknown command
            else
            {
                char response[128];

                snprintf(response,
                         sizeof(response),
                         "ERR 099 UNKNOWN_COMMAND SID:%s\n",
                         SID);

                send_all(client_fd, response);

                printf("Unknown command rejected.\n");
            }
        }

        close(client_fd);

        printf("Controller connection closed.\n");
    }

    close(server_fd);

    return 0;
}
