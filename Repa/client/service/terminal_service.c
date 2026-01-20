#include "terminal_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#ifdef __APPLE__
#include <editline/readline.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif


static void terminal_disable_echo(void) {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

static void terminal_enable_echo(void) {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

char* terminal_read_password(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);

    terminal_disable_echo();

    char *password = NULL;
    size_t len = 0;
    const ssize_t read = getline(&password, &len, stdin);

    terminal_enable_echo();
    printf("\n");

    if (read > 0 && password[read - 1] == '\n') {
        password[read - 1] = '\0';
    }

    return password;
}


char* terminal_read_command(const char *prompt) {
    char *line = readline(prompt);

    if (!line) return NULL;

    if (!*line) return line;

    const char *p = line;
    while (*p && isspace(*p)) p++;

    if (strncasecmp(p, "auth ", 5) == 0) {
        p += 5;
        while (*p && isspace(*p)) p++;

        const char *username_start = p;
        while (*p && !isspace(*p)) p++;

        const size_t username_len = p - username_start;

        if (username_len > 0) {
            char username[256];
            snprintf(username, sizeof(username), "%.*s", (int)username_len, username_start);

            while (*p && isspace(*p)) p++;

            if (*p) {
                printf("\033[A\033[2K");

                char *masked = terminal_mask_password(line);
                printf("%s%s\n", prompt, masked);
                free(masked);

                char *history_masked = terminal_mask_password(line);
                add_history(history_masked);
                free(history_masked);

                return line;
            }

            char *password = terminal_read_password("Password: ");
            if (!password) {
                free(line);
                return NULL;
            }

            char full_cmd[512];
            snprintf(full_cmd, sizeof(full_cmd), "auth %s %s", username, password);

            char history_entry[512];
            snprintf(history_entry, sizeof(history_entry), "auth %s *****", username);
            add_history(history_entry);

            free(line);
            free(password);

            return strdup(full_cmd);
        }
    }

    add_history(line);
    return line;
}


static const char* skip_whitespace(const char *str) {
    while (*str && isspace(*str)) {
        str++;
    }
    return str;
}

static const char* skip_to_whitespace(const char *str) {
    while (*str && !isspace(*str)) {
        str++;
    }
    return str;
}

static int is_auth_command(const char *line) {
    const char *p = skip_whitespace(line);

    if (strncasecmp(p, "auth", 4) != 0) {
        return 0;
    }

    p += 4;

    if (*p && !isspace(*p)) {
        return 0;
    }

    return 1;
}

static const char* find_password_position(const char *line) {
    const char *p = skip_whitespace(line);

    p += 4;

    p = skip_whitespace(p);

    p = skip_to_whitespace(p);

    if (!*p) {
        return NULL;
    }

    p = skip_whitespace(p);

    if (!*p) {
        return NULL;
    }

    return p;
}

char* terminal_mask_password(const char *line) {
    if (!line) {
        return NULL;
    }

    if (!is_auth_command(line)) {
        return strdup(line);
    }

    const char *password_pos = find_password_position(line);
    if (!password_pos) {
        return strdup(line);
    }

    const size_t prefix_len = password_pos - line;
    const size_t result_size = prefix_len + 6;
    char *result = malloc(result_size);
    if (!result) {
        return strdup(line);
    }

    memcpy(result, line, prefix_len);
    strcpy(result + prefix_len, "*****");

    return result;
}
