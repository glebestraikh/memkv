#include "network_listener.h"
#include "../../logger/logger.h"
#include "../../../protocol/resp.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>

#define MAX_CLIENTS 1024
#define BUFFER_SIZE 8192
#define BACKLOG 128

typedef struct {
    int fd;
    int is_authenticated;
    char read_buffer[BUFFER_SIZE];
    size_t read_pos;
    int active;
} client_session_t;

struct network_listener {
    int port;
    int server_fd;
    int workers;
    command_executor_t *executor;

    pthread_t *worker_threads;
    pthread_t accept_thread;

    client_session_t *clients;
    size_t max_clients;
    pthread_mutex_t clients_mutex;

    int running;
    int stop_requested;
};

static int set_nonblocking(const int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_client(const network_listener_t *listener, client_session_t *client) {
    if (client->fd >= 0 && client->active) {
        close(client->fd);
        client->fd = -1;
        client->active = 0;

        stats_dec_connections(listener->executor->stats);
    }
}

static ssize_t read_client_data(client_session_t *client) {
    const ssize_t n = read(client->fd, client->read_buffer + client->read_pos,
                           BUFFER_SIZE - client->read_pos - 1);

    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            return -1;
        }
        return 0;
    }

    client->read_pos += n;
    client->read_buffer[client->read_pos] = '\0';
    return n;
}

static int send_response(const client_session_t *client, const resp_value_t *response) {
    char *output = NULL;
    size_t output_len = 0;
    if (resp_serialize(response, &output, &output_len) != 0) {
        return -1;
    }

    ssize_t sent = 0;
    while (sent < (ssize_t) output_len) {
        const ssize_t written = write(client->fd, output + sent, output_len - sent);
        if (written <= 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                free(output);
                return -1;
            }
            struct pollfd pfd = {.fd = client->fd, .events = POLLOUT};
            poll(&pfd, 1, 100);
            continue;
        }
        sent += written;
    }
    free(output);
    return 0;
}

static int process_single_command(const network_listener_t *listener, client_session_t *client, resp_value_t *cmd) {
    resp_value_t *response = command_executor_execute(
        listener->executor, cmd, &client->is_authenticated);

    if (send_response(client, response) != 0) {
        resp_free(response);
        return -1;
    }

    if (cmd->type == RESP_ARRAY && cmd->data.array.count > 0) {
        const resp_value_t *cmd_name = cmd->data.array.elements[0];
        if (cmd_name->type == RESP_BULK_STRING &&
            strcasecmp(cmd_name->data.str, "QUIT") == 0) {
            resp_free(response);
            return -1;
        }
    }

    resp_free(response);
    return 0;
}

static void shift_buffer(client_session_t *client, const size_t processed) {
    if (processed > 0) {
        memmove(client->read_buffer, client->read_buffer + processed,
                client->read_pos - processed);
        client->read_pos -= processed;
    }
}

static int handle_client_data(const network_listener_t *listener, client_session_t *client) {
    const ssize_t read_result = read_client_data(client);
    if (read_result < 0) {
        return -1;
    }
    if (read_result == 0) {
        return 0;
    }

    size_t processed = 0;
    while (processed < client->read_pos) {
        size_t bytes_consumed = 0;
        resp_value_t *cmd = resp_parse(client->read_buffer + processed, client->read_pos - processed,
                                       &bytes_consumed);

        if (!cmd) {
            break;
        }

        processed += bytes_consumed;

        if (process_single_command(listener, client, cmd) != 0) {
            resp_free(cmd);
            return -1;
        }

        resp_free(cmd);
    }

    shift_buffer(client, processed);

    return 0;
}

typedef struct {
    network_listener_t *listener;
    int worker_id;
} worker_context_t;

static void worker_cleanup_handler(void *arg) {
    struct pollfd **poll_fds_ptr = arg;
    if (poll_fds_ptr && *poll_fds_ptr) {
        free(*poll_fds_ptr);
        *poll_fds_ptr = NULL;
    }
}

static struct pollfd *allocate_poll_fds() {
    struct pollfd *poll_fds = malloc(sizeof(struct pollfd) * MAX_CLIENTS);
    if (!poll_fds) {
        LOG_ERROR_MSG("Failed to allocate poll_fds");
        return NULL;
    }
    return poll_fds;
}

static int is_client_owned_by_worker(const size_t client_index, const int worker_id, const int total_workers) {
    return client_index % total_workers == (size_t) worker_id;
}

static nfds_t collect_active_fds(const network_listener_t *listener, struct pollfd *poll_fds, const int worker_id) {
    nfds_t nfds = 0;
    for (size_t i = 0; i < listener->max_clients; i++) {
        if (!is_client_owned_by_worker(i, worker_id, listener->workers)) {
            continue;
        }

        const client_session_t *client = &listener->clients[i];
        if (client->active && client->fd >= 0) {
            poll_fds[nfds].fd = client->fd;
            poll_fds[nfds].events = POLLIN;
            poll_fds[nfds].revents = 0;
            nfds++;
        }
    }
    return nfds;
}

static void process_clients_with_data(const network_listener_t *listener, const struct pollfd *poll_fds,
                                      const nfds_t nfds, const int worker_id) {
    for (nfds_t j = 0; j < nfds; j++) {
        if (!(poll_fds[j].revents & POLLIN)) {
            continue;
        }

        for (size_t i = 0; i < listener->max_clients; i++) {
            if (!is_client_owned_by_worker(i, worker_id, listener->workers)) {
                continue;
            }

            client_session_t *client = &listener->clients[i];
            if (client->active && client->fd == poll_fds[j].fd) {
                if (handle_client_data(listener, client) != 0) {
                    LOG_INFO_MSG("Client disconnected: fd=%d", client->fd);
                    close_client(listener, client);
                }
                break;
            }
        }
    }
}

static void *worker_thread_func(void *arg) {
    worker_context_t *context = arg;
    network_listener_t *listener = context->listener;
    const int worker_id = context->worker_id;

    LOG_INFO_MSG("Worker thread %d started", worker_id);

    struct pollfd *poll_fds = allocate_poll_fds();
    if (!poll_fds) {
        free(context);
        return NULL;
    }

    pthread_cleanup_push(worker_cleanup_handler, &poll_fds);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

        while (!listener->stop_requested) {
            if (pthread_mutex_lock(&listener->clients_mutex) != 0) {
                LOG_ERROR_MSG("Worker %d: failed to lock clients_mutex", worker_id);
                continue;
            }

            const nfds_t nfds = collect_active_fds(listener, poll_fds, worker_id);

            if (pthread_mutex_unlock(&listener->clients_mutex) != 0) {
                LOG_ERROR_MSG("Worker %d: failed to unlock clients_mutex", worker_id);
            }

            if (nfds > 0) {
                const int poll_result = poll(poll_fds, nfds, 100);

                if (poll_result > 0) {
                    if (pthread_mutex_lock(&listener->clients_mutex) != 0) {
                        LOG_ERROR_MSG("Worker %d: failed to lock clients_mutex for processing", worker_id);
                        continue;
                    }

                    process_clients_with_data(listener, poll_fds, nfds, worker_id);

                    if (pthread_mutex_unlock(&listener->clients_mutex) != 0) {
                        LOG_ERROR_MSG("Worker %d: failed to unlock clients_mutex after processing", worker_id);
                    }
                }
            } else {
                struct pollfd dummy = {.fd = -1, .events = 0};
                poll(&dummy, 0, 100);
            }
        }

    pthread_cleanup_pop(1);
    free(context);
    LOG_INFO_MSG("Worker thread %d finished", worker_id);
    return NULL;
}

static int accept_new_connection(const int server_fd, struct sockaddr_in *client_addr, socklen_t *client_len) {
    const int client_fd = accept(server_fd, (struct sockaddr *) client_addr, client_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR_MSG("Accept failed: %s", strerror(errno));
        }
        return -1;
    }
    return client_fd;
}

static int initialize_client_slot(network_listener_t *listener, const int client_fd,
                                  const struct sockaddr_in *client_addr) {
    LOG_INFO_MSG("New connection from %s:%d (fd=%d)",
                 inet_ntoa(client_addr->sin_addr),
                 ntohs(client_addr->sin_port), client_fd);

    if (pthread_mutex_lock(&listener->clients_mutex) != 0) {
        LOG_ERROR_MSG("Failed to lock clients_mutex in initialize_client_slot");
        return 0;
    }

    int slot_found = 0;
    for (size_t i = 0; i < listener->max_clients; i++) {
        if (!listener->clients[i].active) {
            listener->clients[i].fd = client_fd;
            listener->clients[i].is_authenticated = 0;
            listener->clients[i].read_pos = 0;
            listener->clients[i].active = 1;
            memset(listener->clients[i].read_buffer, 0, BUFFER_SIZE);
            slot_found = 1;

            stats_inc_connections(listener->executor->stats);
            break;
        }
    }

    if (pthread_mutex_unlock(&listener->clients_mutex) != 0) {
        LOG_ERROR_MSG("Failed to unlock clients_mutex in initialize_client_slot");
    }

    return slot_found;
}

static void *accept_thread_func(void *arg) {
    network_listener_t *listener = arg;

    LOG_INFO_MSG("Accept thread started");

    while (!listener->stop_requested) {
        struct pollfd pfd = {.fd = listener->server_fd, .events = POLLIN};
        const int poll_result = poll(&pfd, 1, 100); // 100ms timeout

        if (poll_result <= 0) {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = accept_new_connection(listener->server_fd, &client_addr, &client_len);

        if (client_fd < 0) {
            continue;
        }

        const int slot_found = initialize_client_slot(listener, client_fd, &client_addr);

        if (!slot_found) {
            LOG_WARN_MSG("Too many clients, rejecting connection");
            close(client_fd);
        }
    }

    LOG_INFO_MSG("Accept thread finished");
    return NULL;
}

network_listener_t *network_listener_create(const int port, const int workers, command_executor_t *executor) {
    if (!executor || port <= 0 || workers <= 0) {
        return NULL;
    }

    network_listener_t *listener = malloc(sizeof(network_listener_t));
    if (!listener) {
        return NULL;
    }

    listener->port = port;
    listener->server_fd = -1;
    listener->workers = workers;
    listener->executor = executor;
    listener->running = 0;
    listener->stop_requested = 0;
    listener->max_clients = MAX_CLIENTS;

    listener->clients = calloc(MAX_CLIENTS, sizeof(client_session_t));
    if (!listener->clients) {
        free(listener);
        return NULL;
    }

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        listener->clients[i].fd = -1;
        listener->clients[i].active = 0;
    }

    if (pthread_mutex_init(&listener->clients_mutex, NULL) != 0) {
        LOG_ERROR_MSG("Failed to initialize clients_mutex");
        free(listener->clients);
        free(listener);
        return NULL;
    }

    listener->worker_threads = malloc(sizeof(pthread_t) * workers);
    if (!listener->worker_threads) {
        free(listener->clients);
        free(listener);
        return NULL;
    }

    return listener;
}

int network_listener_start(network_listener_t *listener) {
    if (!listener || listener->running) {
        return -1;
    }

    listener->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener->server_fd < 0) {
        LOG_ERROR_MSG("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    const int opt = 1;
    if (setsockopt(listener->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN_MSG("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(listener->port);

    if (bind(listener->server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR_MSG("Bind failed on port %d: %s", listener->port, strerror(errno));
        close(listener->server_fd);
        return -1;
    }

    if (listen(listener->server_fd, BACKLOG) < 0) {
        LOG_ERROR_MSG("Listen failed: %s", strerror(errno));
        close(listener->server_fd);
        return -1;
    }

    set_nonblocking(listener->server_fd);

    LOG_INFO_MSG("Server listening on port %d", listener->port);

    listener->running = 1;
    listener->stop_requested = 0;

    worker_context_t **contexts = calloc(listener->workers, sizeof(worker_context_t *));
    if (!contexts) {
        LOG_ERROR_MSG("Failed to allocate contexts array");
        return -1;
    }

    for (int i = 0; i < listener->workers; i++) {
        contexts[i] = malloc(sizeof(worker_context_t));
        if (!contexts[i]) {
            LOG_ERROR_MSG("Failed to allocate worker context %d", i);
            listener->stop_requested = 1;

            for (int j = 0; j < i; j++) {
                if (contexts[j]) {
                    free(contexts[j]);
                }
            }
            free(contexts);
            return -1;
        }

        contexts[i]->listener = listener;
        contexts[i]->worker_id = i;

        if (pthread_create(&listener->worker_threads[i], NULL, worker_thread_func, contexts[i]) != 0) {
            LOG_ERROR_MSG("Failed to create worker thread %d", i);
            listener->stop_requested = 1;

            for (int j = 0; j <= i; j++) {
                if (contexts[j]) {
                    free(contexts[j]);
                }
            }
            free(contexts);

            for (int j = 0; j < i; j++) {
                pthread_cancel(listener->worker_threads[j]);
            }
            for (int j = 0; j < i; j++) {
                pthread_join(listener->worker_threads[j], NULL);
            }
            return -1;
        }
    }

    free(contexts);

    if (pthread_create(&listener->accept_thread, NULL, accept_thread_func, listener) != 0) {
        LOG_ERROR_MSG("Failed to create accept thread");
        listener->stop_requested = 1;

        for (int i = 0; i < listener->workers; i++) {
            pthread_cancel(listener->worker_threads[i]);
        }
        for (int i = 0; i < listener->workers; i++) {
            pthread_join(listener->worker_threads[i], NULL);
        }
        return -1;
    }

    return 0;
}

void network_listener_stop(network_listener_t *listener, const int timeout_sec) {
    if (!listener || !listener->running) {
        return;
    }

    LOG_INFO_MSG("Received shutdown signal");
    LOG_INFO_MSG("Step 1: Closing server socket (stopping new connections)");

    listener->stop_requested = 1;

    if (listener->server_fd >= 0) {
        shutdown(listener->server_fd, SHUT_RDWR);
        close(listener->server_fd);
        listener->server_fd = -1;
    }

    LOG_INFO_MSG("Step 2: Waiting for threads to finish (up to %d seconds)", timeout_sec);

    const time_t start_time = time(NULL);

    {
        LOG_DEBUG_MSG("Waiting for accept thread...");
        const time_t accept_start = time(NULL);
        const time_t elapsed = accept_start - start_time;
        const int remaining = timeout_sec - (int) elapsed;

        if (remaining > 0) {
            void *status = NULL;
            const int result = pthread_join(listener->accept_thread, &status);

            if (result == 0) {
                LOG_DEBUG_MSG("Accept thread finished gracefully");
            } else {
                LOG_WARN_MSG("Failed to join accept thread gracefully, cancelling");
                pthread_cancel(listener->accept_thread);
                pthread_join(listener->accept_thread, NULL);
            }
        } else {
            LOG_WARN_MSG("Accept thread timeout exceeded, cancelling");
            pthread_cancel(listener->accept_thread);
            pthread_join(listener->accept_thread, NULL);
        }
    }

    for (int i = 0; i < listener->workers; i++) {
        const time_t elapsed = time(NULL) - start_time;
        const int remaining = (int) (timeout_sec - elapsed);

        if (remaining <= 0) {
            LOG_WARN_MSG("Worker thread %d: timeout exceeded, cancelling", i);
            pthread_cancel(listener->worker_threads[i]);
            pthread_join(listener->worker_threads[i], NULL);
            continue;
        }

        LOG_DEBUG_MSG("Waiting for worker thread %d (remaining: %d seconds)...", i, remaining);

        void *status = NULL;
        const int result = pthread_join(listener->worker_threads[i], &status);

        if (result == 0) {
            LOG_DEBUG_MSG("Worker thread %d finished gracefully", i);
        } else {
            LOG_WARN_MSG("Worker thread %d timeout or error, cancelling", i);
            pthread_cancel(listener->worker_threads[i]);
            pthread_join(listener->worker_threads[i], NULL);
        }
    }

    LOG_INFO_MSG("Step 3: Closing all client connections");

    if (pthread_mutex_lock(&listener->clients_mutex) != 0) {
        LOG_ERROR_MSG("Failed to lock clients_mutex in network_listener_stop");
        return;
    }

    int closed_count = 0;
    for (size_t i = 0; i < listener->max_clients; i++) {
        if (listener->clients[i].active) {
            LOG_DEBUG_MSG("Closing client connection: fd=%d", listener->clients[i].fd);
            close_client(listener, &listener->clients[i]);
            closed_count++;
        }
    }

    if (pthread_mutex_unlock(&listener->clients_mutex) != 0) {
        LOG_ERROR_MSG("Failed to unlock clients_mutex in network_listener_stop");
    }

    LOG_INFO_MSG("Closed %d client connections", closed_count);

    listener->running = 0;

    LOG_INFO_MSG("All threads have finished");
}

void network_listener_destroy(network_listener_t *listener) {
    if (!listener) {
        return;
    }

    if (listener->running) {
        network_listener_stop(listener, 5);
    }

    pthread_mutex_destroy(&listener->clients_mutex);

    free(listener->worker_threads);
    free(listener->clients);
    free(listener);
}
