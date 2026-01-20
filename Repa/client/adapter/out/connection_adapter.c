#include "connection_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define BUFFER_SIZE 8192
#define MAX_TOKENS 64

struct connection {
    int sockfd;
    char *addr;
    int port;
};

static int create_socket(connection_t *conn) {
    conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sockfd < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int setup_and_connect(const connection_t *conn, const char *addr, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, addr, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", addr);
        return -1;
    }

    if (connect(conn->sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Connection failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int send_hello_command(const int sockfd) {
    resp_value_t *hello_cmd = resp_create_array(2);
    resp_array_set(hello_cmd, 0, resp_create_bulk_string("HELLO", 5));
    resp_array_set(hello_cmd, 1, resp_create_bulk_string("2", 1));

    char *hello_buf = NULL;
    size_t hello_len = 0;
    int result = -1;
    if (resp_serialize(hello_cmd, &hello_buf, &hello_len) == 0) {
        if (write(sockfd, hello_buf, hello_len) > 0) {
            result = 0;
        }
        free(hello_buf);
    }
    resp_free(hello_cmd);
    return result;
}

static int receive_hello_response(const int sockfd) {
    char buffer[BUFFER_SIZE];
    const ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
    if (n <= 0) {
        if (n == 0) {
            fprintf(stderr, "Connection closed by server during HELLO\n");
        } else {
            fprintf(stderr, "Read error during HELLO: %s\n", strerror(errno));
        }
        return -1;
    }

    buffer[n] = '\0';
    size_t consumed = 0;
    resp_value_t *response = resp_parse(buffer, n, &consumed);
    if (!response) {
        fprintf(stderr, "Failed to parse HELLO response\n");
        return -1;
    }

    int result = 0;
    if (response->type == RESP_ERROR) {
        fprintf(stderr, "HELLO failed: %s\n", response->data.str);
        result = -1;
    }

    resp_free(response);
    return result;
}

connection_t* connection_create(const char *addr, const int port) {
    if (!addr || port <= 0) {
        return NULL;
    }

    connection_t *conn = malloc(sizeof(connection_t));
    if (!conn) {
        return NULL;
    }

    if (create_socket(conn) != 0) {
        free(conn);
        return NULL;
    }

    if (setup_and_connect(conn, addr, port) != 0) {
        close(conn->sockfd);
        free(conn);
        return NULL;
    }

    conn->addr = strdup(addr);
    conn->port = port;

    if (send_hello_command(conn->sockfd) != 0) {
        close(conn->sockfd);
        free(conn->addr);
        free(conn);
        return NULL;
    }

    if (receive_hello_response(conn->sockfd) != 0) {
        close(conn->sockfd);
        free(conn->addr);
        free(conn);
        return NULL;
    }

    return conn;
}

static char* parse_command_tokens(const char *command, char **tokens, int *token_count_out) {
    char *line_copy = strdup(command);
    if (!line_copy) {
        *token_count_out = 0;
        return NULL;
    }

    int token_count = 0;
    char *token = strtok(line_copy, " \t");
    while (token && token_count < MAX_TOKENS) {
        tokens[token_count++] = token;
        token = strtok(NULL, " \t");
    }

    *token_count_out = token_count;
    return line_copy;
}

static resp_value_t* create_resp_command(char **tokens, const int token_count) {
    resp_value_t *cmd = resp_create_array(token_count);
    if (!cmd) {
        return NULL;
    }

    for (int i = 0; i < token_count; i++) {
        resp_array_set(cmd, i, resp_create_bulk_string(tokens[i], strlen(tokens[i])));
    }

    return cmd;
}

static int send_command(const int sockfd, const resp_value_t *cmd) {
    char *cmd_buf = NULL;
    size_t cmd_len = 0;
    if (resp_serialize(cmd, &cmd_buf, &cmd_len) != 0) {
        return -1;
    }

    const ssize_t written = write(sockfd, cmd_buf, cmd_len);
    free(cmd_buf);

    return written > 0 ? 0 : -1;
}

static resp_value_t* receive_response(const int sockfd) {
    char buffer[BUFFER_SIZE];
    size_t total_read = 0;
    resp_value_t *response = NULL;

    while (!response) {
        const ssize_t n = read(sockfd, buffer + total_read, BUFFER_SIZE - total_read - 1);
        if (n <= 0) {
            if (n == 0) {
                fprintf(stderr, "Connection closed by server\n");
            } else {
                fprintf(stderr, "Read error: %s\n", strerror(errno));
            }
            break;
        }

        total_read += n;
        buffer[total_read] = '\0';

        size_t consumed = 0;
        response = resp_parse(buffer, total_read, &consumed);

        if (response) {
            break;
        }

        if (total_read >= BUFFER_SIZE - 100) {
            fprintf(stderr, "Response too large or malformed\n");
            break;
        }
    }

    return response;
}

resp_value_t* connection_execute_command(connection_t *conn, const char *command) {
    if (!conn || !command) {
        return NULL;
    }

    char *tokens[MAX_TOKENS];
    int token_count;
    char *line_copy = parse_command_tokens(command, tokens, &token_count);

    if (token_count == 0) {
        free(line_copy);
        return NULL;
    }

    resp_value_t *cmd = create_resp_command(tokens, token_count);
    if (!cmd) {
        free(line_copy);
        return NULL;
    }

    if (send_command(conn->sockfd, cmd) != 0) {
        resp_free(cmd);
        free(line_copy);
        return NULL;
    }

    resp_free(cmd);
    free(line_copy);

    return receive_response(conn->sockfd);
}

void connection_close(connection_t *conn) {
    if (!conn) {
        return;
    }

    resp_value_t *quit_cmd = resp_create_array(1);
    resp_array_set(quit_cmd, 0, resp_create_bulk_string("QUIT", 4));

    char *quit_buf = NULL;
    size_t quit_len = 0;
    if (resp_serialize(quit_cmd, &quit_buf, &quit_len) == 0) {
        write(conn->sockfd, quit_buf, quit_len);
        free(quit_buf);
    }
    resp_free(quit_cmd);

    if (conn->sockfd >= 0) {
        close(conn->sockfd);
    }
    free(conn->addr);
    free(conn);
}
