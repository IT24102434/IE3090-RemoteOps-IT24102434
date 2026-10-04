#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
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

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in server_addr;

    char token[100];
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if (argc != 2)
    {
        printf("Usage: %s <Agent_IP>\n", argv[0]);
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  argv[1],
                  &server_addr.sin_addr) <= 0)
    {
        printf("Invalid Agent IP address\n");
        close(sock);
        return 1;
    }

    printf("RemoteOps Controller starting...\n");
    printf("Connecting to Agent %s:%d...\n",
           argv[1],
           PORT);

    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("Connected to RemoteOps Agent successfully.\n");

    printf("Enter authentication token: ");
    scanf("%99s", token);

    snprintf(command,
             sizeof(command),
             "AUTH %s\n",
             token);

    send(sock,
         command,
         strlen(command),
         0);

    int result =
        recv_line(sock,
                  response,
                  sizeof(response));

    if (result <= 0)
    {
        printf("No authentication response received.\n");
        close(sock);
        return 1;
    }

    printf("Agent response: %s\n", response);

    if (strncmp(response,
                "OK AUTHENTICATED",
                16) != 0)
    {
        printf("Authentication failed. Closing Controller.\n");
        close(sock);
        return 1;
    }

    // Clear leftover newline from scanf input
    getchar();

    while (1)
    {
        printf("\nRemoteOps> ");

        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            break;
        }

        // If user only presses Enter
        if (strcmp(command, "\n") == 0)
        {
            continue;
        }

        send(sock,
             command,
             strlen(command),
             0);

        result =
            recv_line(sock,
                      response,
                      sizeof(response));

        if (result <= 0)
        {
            printf("Agent disconnected.\n");
            break;
        }

        printf("Agent response: %s\n", response);

        if (strncmp(command, "QUIT", 4) == 0)
        {
            break;
        }
    }

    close(sock);

    return 0;
}
