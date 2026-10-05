# include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define PORT 9410
#define BUFFER_SIZE 1024

/* --------------------------------------------------
   Receive one protocol line
   -------------------------------------------------- */
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

/* --------------------------------------------------
   Send exactly all bytes
   -------------------------------------------------- */
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
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}

/* --------------------------------------------------
   Receive exactly a requested number of bytes
   -------------------------------------------------- */
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
        {
            return -1;
        }

        total += received;
    }

    return total;
}

/* --------------------------------------------------
   PUT upload
   -------------------------------------------------- */
int upload_file(int sock,
                const char *filename)
{
    struct stat file_info;

    char header[512];
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    /*
       Check local file
    */
    if (stat(filename,
             &file_info) < 0)
    {
        printf(
            "Local file not found: %s\n",
            filename
        );

        return -1;
    }

    if (!S_ISREG(
            file_info.st_mode
        ))
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
        perror(
            "Unable to open local file"
        );

        return -1;
    }

    long long filesize =
        (long long)
        file_info.st_size;

    /*
       Send only basename to Agent
    */
    const char *base_name =
        strrchr(filename, '/');

    if (base_name != NULL)
    {
        base_name++;
    }
    else
    {
        base_name = filename;
    }

    snprintf(
        header,
        sizeof(header),
        "PUT %s %lld\n",
        base_name,
        filesize
    );

    printf(
        "Sending protocol: PUT %s %lld\n",
        base_name,
        filesize
    );

    /*
       Send PUT header
    */
    if (send_bytes(
            sock,
            header,
            strlen(header)
        ) < 0)
    {
        printf(
            "Failed to send PUT header.\n"
        );

        fclose(fp);

        return -1;
    }

    /*
       Send file bytes
    */
    long long total_sent = 0;

    while (total_sent < filesize)
    {
        size_t bytes_read =
            fread(buffer,
                  1,
                  sizeof(buffer),
                  fp);

        if (bytes_read == 0)
        {
            if (ferror(fp))
            {
                printf(
                    "Error reading local file.\n"
                );

                fclose(fp);

                return -1;
            }

            break;
        }

        if (send_bytes(
                sock,
                buffer,
                bytes_read
            ) < 0)
        {
            printf(
                "File transmission failed.\n"
            );

            fclose(fp);

            return -1;
        }

        total_sent +=
            bytes_read;
    }

    fclose(fp);

    printf(
        "Uploaded %lld raw bytes.\n",
        total_sent
    );

    /*
       Receive PUT result
    */
    int result =
        recv_line(sock,
                  response,
                  sizeof(response));

    if (result <= 0)
    {
        printf(
            "No PUT response received from Agent.\n"
        );

        return -1;
    }

    printf(
        "Agent response: %s\n",
        response
    );

    return 0;
}

/* --------------------------------------------------
   GET download
   -------------------------------------------------- */
int download_file(int sock,
                  const char *filename)
{
    char request[512];
    char response[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    /*
       Send GET command
    */
    snprintf(
        request,
        sizeof(request),
        "GET %s\n",
        filename
    );

    if (send_bytes(
            sock,
            request,
            strlen(request)
        ) < 0)
    {
        printf(
            "Failed to send GET request.\n"
        );

        return -1;
    }

    /*
       Receive Agent response line
    */
    int result =
        recv_line(sock,
                  response,
                  sizeof(response));

    if (result <= 0)
    {
        printf(
            "No GET response received from Agent.\n"
        );

        return -1;
    }

    /*
       Check ERR response
    */
    if (strncmp(
            response,
            "ERR ",
            4
        ) == 0)
    {
        printf(
            "Agent response: %s\n",
            response
        );

        return -1;
    }

    char returned_filename[256];
    long long filesize;
    char sid_value[64];

    int parsed =
        sscanf(
            response,
            "OK FILE_SEND %255s %lld SID:%63s",
            returned_filename,
            &filesize,
            sid_value
        );

    if (parsed != 3)
    {
        printf(
            "Invalid GET response: %s\n",
            response
        );

        return -1;
    }

    printf(
        "Agent response: %s\n",
        response
    );

    /*
       Save using different filename
    */
    char output_name[512];

    snprintf(
        output_name,
        sizeof(output_name),
        "downloaded_%s",
        returned_filename
    );

    FILE *fp =
        fopen(output_name, "wb");

    if (fp == NULL)
    {
        perror(
            "Unable to create downloaded file"
        );

        return -1;
    }

    /*
       Receive exactly filesize bytes
    */
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
            recv_exact(
                sock,
                buffer,
                amount
            );

        if (received <= 0)
        {
            printf(
                "GET transfer failed.\n"
            );

            fclose(fp);

            remove(output_name);

            return -1;
        }

        size_t written =
            fwrite(
                buffer,
                1,
                received,
                fp
            );

        if (written !=
            (size_t)received)
        {
            printf(
                "Error writing downloaded file.\n"
            );

            fclose(fp);

            remove(output_name);

            return -1;
        }

        remaining -= received;

        total_received +=
            received;
    }

    fclose(fp);

    printf(
        "Downloaded %lld raw bytes.\n",
        total_received
    );

    printf(
        "Saved as: %s\n",
        output_name
    );

    return 0;
}

/* --------------------------------------------------
   MAIN CONTROLLER
   -------------------------------------------------- */
int main(int argc,
         char *argv[])
{
    int sock;

    struct sockaddr_in
        server_addr;

    char token[100];

    char command[
        BUFFER_SIZE
    ];

    char response[
        BUFFER_SIZE
    ];

    if (argc != 2)
    {
        printf(
            "Usage: %s <Agent_IP>\n",
            argv[0]
        );

        return 1;
    }

    /*
       Create TCP socket
    */
    sock =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (sock < 0)
    {
        perror(
            "Socket creation failed"
        );

        return 1;
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(PORT);

    if (inet_pton(
            AF_INET,
            argv[1],
            &server_addr.sin_addr
        ) <= 0)
    {
        printf(
            "Invalid Agent IP address\n"
        );

        close(sock);

        return 1;
    }

    printf(
        "RemoteOps Controller starting...\n"
    );

    printf(
        "Connecting to Agent %s:%d...\n",
        argv[1],
        PORT
    );

    /*
       Connect to Agent
    */
    if (connect(
            sock,
            (struct sockaddr *)
            &server_addr,
            sizeof(server_addr)
        ) < 0)
    {
        perror(
            "Connection failed"
        );

        close(sock);

        return 1;
    }

    printf(
        "Connected to RemoteOps Agent successfully.\n"
    );

    /* --------------------------------------------------
       AUTHENTICATION
       -------------------------------------------------- */
    printf(
        "Enter authentication token: "
    );

    scanf(
        "%99s",
        token
    );

    snprintf(
        command,
        sizeof(command),
        "AUTH %s\n",
        token
    );

    if (send_bytes(
            sock,
            command,
            strlen(command)
        ) < 0)
    {
        printf(
            "Failed to send authentication command.\n"
        );

        close(sock);

        return 1;
    }

    int result =
        recv_line(
            sock,
            response,
            sizeof(response)
        );

    if (result <= 0)
    {
        printf(
            "No authentication response received.\n"
        );

        close(sock);

        return 1;
    }

    printf(
        "Agent response: %s\n",
        response
    );

    if (strncmp(
            response,
            "OK AUTHENTICATED",
            16
        ) != 0)
    {
        printf(
            "Authentication failed. Closing Controller.\n"
        );

        close(sock);

        return 1;
    }

    /*
       Remove newline left by scanf
    */
    getchar();

    /* --------------------------------------------------
       Interactive command loop
       -------------------------------------------------- */
    while (1)
    {
        printf(
            "\nRemoteOps> "
        );

        if (fgets(
                command,
                sizeof(command),
                stdin
            ) == NULL)
        {
            break;
        }

        /*
           Remove user-entered newline
        */
        command[
            strcspn(
                command,
                "\n"
            )
        ] = '\0';

        if (strlen(command) == 0)
        {
            continue;
        }

        /* ----------------------------------------------
           PUT
           User enters:
           PUT sample.txt
           ---------------------------------------------- */
        if (strncmp(
                command,
                "PUT ",
                4
            ) == 0)
        {
            char filename[512];

            if (sscanf(
                    command,
                    "PUT %511s",
                    filename
                ) != 1)
            {
                printf(
                    "Usage: PUT <filename>\n"
                );

                continue;
            }

            upload_file(
                sock,
                filename
            );

            continue;
        }

        /* ----------------------------------------------
           GET
           User enters:
           GET sample.txt
           ---------------------------------------------- */
        if (strncmp(
                command,
                "GET ",
                4
            ) == 0)
        {
            char filename[512];

            if (sscanf(
                    command,
                    "GET %511s",
                    filename
                ) != 1)
            {
                printf(
                    "Usage: GET <filename>\n"
                );

                continue;
            }

            download_file(
                sock,
                filename
            );

            continue;
        }

        /* --------------------------------------------------
           Normal line-based commands
           -------------------------------------------------- */
        char protocol_command[
            BUFFER_SIZE
        ];

        snprintf(
            protocol_command,
            sizeof(protocol_command),
            "%s\n",
            command
        );

        if (send_bytes(
                sock,
                protocol_command,
                strlen(protocol_command)
            ) < 0)
        {
            printf(
                "Failed to send command.\n"
            );

            break;
        }

        result =
            recv_line(
                sock,
                response,
                sizeof(response)
            );

        if (result <= 0)
        {
            printf(
                "Agent disconnected.\n"
            );

            break;
        }

        printf(
            "Agent response: %s\n",
            response
        );

        if (strcmp(
                command,
                "QUIT"
            ) == 0)
        {
            break;
        }
    }

    close(sock);

    return 0;
}
