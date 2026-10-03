#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in server_addr;

    if (argc != 2)
    {
        printf("Usage: %s <Agent_IP>\n", argv[0]);
        return 1;
    }

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    printf("RemoteOps Controller starting...\n");

    // Configure Agent address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert Agent IP address
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        printf("Invalid Agent IP address\n");
        close(sock);
        return 1;
    }

    printf("Connecting to Agent %s:%d...\n", argv[1], PORT);

    // Connect to Agent
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("Connected to RemoteOps Agent successfully.\n");

    close(sock);

    return 0;
}
