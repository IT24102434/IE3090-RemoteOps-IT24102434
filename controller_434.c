#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/time.h>

#define PORT 9410
#define BUFFER_SIZE 1024

typedef struct
{
    int active;
    int udp_sock;
    int udp_port;
    pthread_t thread_id;

} UDPListener;


// Receive one TCP line
int recv_line(int sock,
              char *buffer,
              int size)
{
    int index = 0;
    char ch;
    int received;

    while (index < size - 1)
    {
        received =
            recv(sock,
                 &ch,
                 1,
                 0);

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


// Send all bytes
int send_bytes(int sock,
               const char *buffer,
               int length)
{
    int total = 0;

    while (total < length)
    {
        int sent =
            send(sock,
                 buffer + total,
                 length - total,
                 0);

        if (sent <= 0)
            return -1;

        total += sent;
    }

    return 0;
}


// Receive exactly a requested number of bytes
int recv_exact(int sock,
               char *buffer,
               int length)
{
    int total = 0;

    while (total < length)
    {
        int received =
            recv(sock,
                 buffer + total,
                 length - total,
                 0);

        if (received <= 0)
            return -1;

        total += received;
    }

    return total;
}


// Upload file using PUT
int upload_file(int sock,
                const char *filename)
{
    struct stat file_info;

    char header[512];
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if (stat(filename,
             &file_info) < 0)
    {
        printf("Local file not found: %s\n",
               filename);

        return -1;
    }

    if (!S_ISREG(file_info.st_mode))
    {
        printf(
            "The selected path is not a regular file.\n"
        );

        return -1;
    }

    FILE *fp =
        fopen(filename, "rb");

    if (fp == NULL)
    {
        perror("Unable to open local file");

        return -1;
    }

    long long filesize =
        (long long)file_info.st_size;

    const char *base_name =
        strrchr(filename, '/');

    if (base_name)
        base_name++;
    else
        base_name = filename;

    snprintf(header,
             sizeof(header),
             "PUT %s %lld\n",
             base_name,
             filesize);

    printf("Sending protocol: PUT %s %lld\n",
           base_name,
           filesize);

    if (send_bytes(sock,
                   header,
                   strlen(header)) < 0)
    {
        fclose(fp);

        return -1;
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

        if (send_bytes(sock,
                       buffer,
                       bytes_read) < 0)
        {
            fclose(fp);

            return -1;
        }

        total_sent += bytes_read;
    }

    fclose(fp);

    printf("Uploaded %lld raw bytes.\n",
           total_sent);

    if (recv_line(sock,
                  response,
                  sizeof(response)) <= 0)
    {
        return -1;
    }

    printf("Agent response: %s\n",
           response);

    return 0;
}


// Download file using GET
int download_file(int sock,
                  const char *filename)
{
    char request[512];
    char response[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    snprintf(request,
             sizeof(request),
             "GET %s\n",
             filename);

    if (send_bytes(sock,
                   request,
                   strlen(request)) < 0)
    {
        return -1;
    }

    if (recv_line(sock,
                  response,
                  sizeof(response)) <= 0)
    {
        return -1;
    }

    if (strncmp(response,
                "ERR ",
                4) == 0)
    {
        printf("Agent response: %s\n",
               response);

        return -1;
    }

    char returned_filename[256];

    long long filesize;

    char sid_value[64];

    if (sscanf(response,
               "OK FILE_SEND %255s %lld SID:%63s",
               returned_filename,
               &filesize,
               sid_value) != 3)
    {
        printf("Invalid GET response.\n");

        return -1;
    }

    printf("Agent response: %s\n",
           response);

    char output_name[512];

    snprintf(output_name,
             sizeof(output_name),
             "downloaded_%s",
             returned_filename);

    FILE *fp =
        fopen(output_name, "wb");

    if (fp == NULL)
        return -1;

    long long remaining =
        filesize;

    long long total_received =
        0;

    while (remaining > 0)
    {
        int amount =
            remaining > BUFFER_SIZE
            ? BUFFER_SIZE
            : (int)remaining;

        int received =
            recv_exact(sock,
                       buffer,
                       amount);

        if (received <= 0)
        {
            fclose(fp);

            remove(output_name);

            return -1;
        }

        fwrite(buffer,
               1,
               received,
               fp);

        remaining -= received;

        total_received += received;
    }

    fclose(fp);

    printf("Downloaded %lld raw bytes.\n",
           total_received);

    printf("Saved as: %s\n",
           output_name);

    return 0;
}


// Receive UDP monitoring datagrams
void *udp_listener_thread(void *arg)
{
    UDPListener *listener =
        (UDPListener *)arg;

    char buffer[BUFFER_SIZE];

    while (listener->active)
    {
        struct sockaddr_in sender_addr;

        socklen_t sender_len =
            sizeof(sender_addr);

        int received =
            recvfrom(listener->udp_sock,
                     buffer,
                     sizeof(buffer) - 1,
                     0,
                     (struct sockaddr *)
                     &sender_addr,
                     &sender_len);

        if (received > 0)
        {
            buffer[received] = '\0';

            printf(
                "\n[UDP Monitor] %s\n",
                buffer
            );

            printf("RemoteOps> ");

            fflush(stdout);
        }
    }

    return NULL;
}


// Start Controller UDP listener
int start_udp_listener(UDPListener *listener,
                       int udp_port)
{
    if (listener->active)
        return -1;

    listener->udp_sock =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (listener->udp_sock < 0)
    {
        perror("UDP socket creation failed");

        return -1;
    }

    int opt = 1;

    setsockopt(listener->udp_sock,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    struct timeval timeout;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    setsockopt(listener->udp_sock,
               SOL_SOCKET,
               SO_RCVTIMEO,
               &timeout,
               sizeof(timeout));

    struct sockaddr_in addr;

    memset(&addr,
           0,
           sizeof(addr));

    addr.sin_family =
        AF_INET;

    addr.sin_addr.s_addr =
        INADDR_ANY;

    addr.sin_port =
        htons(udp_port);

    if (bind(listener->udp_sock,
             (struct sockaddr *)
             &addr,
             sizeof(addr)) < 0)
    {
        perror("UDP bind failed");

        close(listener->udp_sock);

        return -1;
    }

    listener->udp_port =
        udp_port;

    listener->active = 1;

    if (pthread_create(
            &listener->thread_id,
            NULL,
            udp_listener_thread,
            listener) != 0)
    {
        listener->active = 0;

        close(listener->udp_sock);

        return -1;
    }

    return 0;
}


// Stop Controller UDP listener
void stop_udp_listener(UDPListener *listener)
{
    if (!listener->active)
        return;

    listener->active = 0;

    pthread_join(listener->thread_id,
                 NULL);

    close(listener->udp_sock);

    listener->udp_sock = -1;
}


// Main Controller program
int main(int argc,
         char *argv[])
{
    int sock;

    struct sockaddr_in server_addr;

    char token[100];
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    UDPListener listener;

    memset(&listener,
           0,
           sizeof(listener));

    listener.udp_sock = -1;

    if (argc != 2)
    {
        printf("Usage: %s <Agent_IP>\n",
               argv[0]);

        return 1;
    }

    sock =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sock < 0)
    {
        perror("Socket creation failed");

        return 1;
    }

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(PORT);

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
                (struct sockaddr *)
                &server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("Connection failed");

        close(sock);

        return 1;
    }

    printf(
        "Connected to RemoteOps Agent successfully.\n"
    );

    // Authentication
    printf("Enter authentication token: ");

    scanf("%99s", token);

    snprintf(command,
             sizeof(command),
             "AUTH %s\n",
             token);

    send_bytes(sock,
               command,
               strlen(command));

    if (recv_line(sock,
                  response,
                  sizeof(response)) <= 0)
    {
        close(sock);

        return 1;
    }

    printf("Agent response: %s\n",
           response);

    if (strncmp(response,
                "OK AUTHENTICATED",
                16) != 0)
    {
        printf("Authentication failed.\n");

        close(sock);

        return 1;
    }

    getchar();

    // Interactive command loop
    while (1)
    {
        printf("\nRemoteOps> ");

        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            break;
        }

        command[
            strcspn(command, "\n")
        ] = '\0';

        if (strlen(command) == 0)
            continue;

        // PUT command
        if (strncmp(command,
                    "PUT ",
                    4) == 0)
        {
            char filename[512];

            if (sscanf(command,
                       "PUT %511s",
                       filename) == 1)
            {
                upload_file(sock,
                            filename);
            }

            continue;
        }

        // GET command
        if (strncmp(command,
                    "GET ",
                    4) == 0)
        {
            char filename[512];

            if (sscanf(command,
                       "GET %511s",
                       filename) == 1)
            {
                download_file(sock,
                              filename);
            }

            continue;
        }

        // MONITOR START
        if (strncmp(command,
                    "MONITOR START ",
                    14) == 0)
        {
            int udp_port;

            if (sscanf(command,
                       "MONITOR START %d",
                       &udp_port) != 1)
            {
                printf(
                    "Usage: MONITOR START <udp_port>\n"
                );

                continue;
            }

            if (start_udp_listener(
                    &listener,
                    udp_port) != 0)
            {
                printf(
                    "Unable to start local UDP listener.\n"
                );

                continue;
            }

            char protocol_command[
                BUFFER_SIZE
            ];

            snprintf(protocol_command,
                     sizeof(protocol_command),
                     "%s\n",
                     command);

            send_bytes(sock,
                       protocol_command,
                       strlen(protocol_command));

            if (recv_line(sock,
                          response,
                          sizeof(response)) <= 0)
            {
                stop_udp_listener(&listener);

                break;
            }

            printf("Agent response: %s\n",
                   response);

            if (strncmp(response,
                        "OK MONITOR_STARTED",
                        18) != 0)
            {
                stop_udp_listener(&listener);
            }

            continue;
        }

        // MONITOR STOP
        if (strcmp(command,
                   "MONITOR STOP") == 0)
        {
            char protocol_command[64];

            snprintf(protocol_command,
                     sizeof(protocol_command),
                     "MONITOR STOP\n");

            send_bytes(sock,
                       protocol_command,
                       strlen(protocol_command));

            if (recv_line(sock,
                          response,
                          sizeof(response)) <= 0)
            {
                stop_udp_listener(&listener);

                break;
            }

            printf("Agent response: %s\n",
                   response);

            stop_udp_listener(&listener);

            continue;
        }

        // Other normal TCP commands
        char protocol_command[
            BUFFER_SIZE
        ];

        snprintf(protocol_command,
                 sizeof(protocol_command),
                 "%s\n",
                 command);

        if (send_bytes(sock,
                       protocol_command,
                       strlen(protocol_command)) < 0)
        {
            break;
        }

        if (recv_line(sock,
                      response,
                      sizeof(response)) <= 0)
        {
            break;
        }

        printf("Agent response: %s\n",
               response);

        // QUIT
        if (strcmp(command,
                   "QUIT") == 0)
        {
            stop_udp_listener(&listener);

            break;
        }
    }

    stop_udp_listener(&listener);

    close(sock);

    return 0;
}
