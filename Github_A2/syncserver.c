#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

//RUN: ./syncserver <sync_dir> <port> <max_clients>
//Compile: gcc syncserver.c -o syncserver

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
#define MAX_CLIENTS 5
#define MAX_WATCHES 1024

// Mapping from watch descriptor to its relative path (from base_directory)
typedef struct {
    int wd;
    char rel_path[512];
} WatchMapping;

WatchMapping watch_mappings[MAX_WATCHES];
int mapping_count = 0;

typedef struct {
    int socket;
    char ignore_list[256];  // Comma-separated list (e.g., ".mp4,.zip")
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int inotify_fd;
char base_directory[512];

/* Helper: remove trailing '/' characters from a path */
void normalize_path(char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len-1] == '/') {
        path[len-1] = '\0';
        len--;
    }
}

/* Classic ignore function */
int should_ignore(const char *filename, const char *ignore_list) {
    const char *ext = strrchr(filename, '.'); //gets the last occurrence of the dot
    if (!ext) return 0;
    char ignore_copy[256];
    strncpy(ignore_copy, ignore_list, sizeof(ignore_copy));
    char *token = strtok(ignore_copy, ","); //splits ignore list by commas
    while (token) {
        if (strcmp(token, ext) == 0)
            return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

/* Recursively adds a directory and its subdirectories to inotify.
   abs_path: absolute path of the directory
   rel_path: path relative to base_directory ("" for base)
*/
void add_watch_recursive(const char *abs_path, const char *rel_path) {
    int wd = inotify_add_watch(inotify_fd, abs_path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
    if (wd >= 0 && mapping_count < MAX_WATCHES) {
        watch_mappings[mapping_count].wd = wd;
        strncpy(watch_mappings[mapping_count].rel_path, rel_path, sizeof(watch_mappings[mapping_count].rel_path)-1);
        watch_mappings[mapping_count].rel_path[sizeof(watch_mappings[mapping_count].rel_path)-1] = '\0';
        mapping_count++;
    }
    
    DIR *dir = opendir(abs_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
             continue;
         char child_abs[512], child_rel[512];
         snprintf(child_abs, sizeof(child_abs), "%s/%s", abs_path, entry->d_name);
         if (strlen(rel_path) == 0)
             snprintf(child_rel, sizeof(child_rel), "%s", entry->d_name);
         else
             snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_path, entry->d_name);
         struct stat st;
         if (stat(child_abs, &st) == 0 && S_ISDIR(st.st_mode)) {
              add_watch_recursive(child_abs, child_rel);
         }
    }
    closedir(dir);
}

/* Recursively scans a directory and broadcasts CREATE events for all items.
   Used when a directory is moved in from outside.
*/
void scan_and_broadcast_creation(const char *abs_path, const char *rel_path) {
    struct stat st;
    if (stat(abs_path, &st) != 0) //use stat to get metadata information
        return;
    if (S_ISDIR(st.st_mode)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "CREATE DIR %s\n", rel_path);
        pthread_mutex_lock(&lock);
        for (int j = 0; j < client_count; j++) {
            if (!should_ignore(rel_path, clients[j].ignore_list))
                send(clients[j].socket, msg, strlen(msg), 0);
        }
        pthread_mutex_unlock(&lock);
        add_watch_recursive(abs_path, rel_path);
        DIR *dir = opendir(abs_path);
        if (!dir) return;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char child_abs[512], child_rel[512];
            snprintf(child_abs, sizeof(child_abs), "%s/%s", abs_path, entry->d_name);
            if (strlen(rel_path) == 0)
                snprintf(child_rel, sizeof(child_rel), "%s", entry->d_name);
            else
                snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_path, entry->d_name);
            scan_and_broadcast_creation(child_abs, child_rel);
        }
        closedir(dir);
    } else {
        FILE *fp = fopen(abs_path, "rb");
        if (!fp) return;
        fseek(fp, 0, SEEK_END);
        long filesize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char header[512];
        snprintf(header, sizeof(header), "CREATE FILE %s %ld\n", rel_path, filesize);
        pthread_mutex_lock(&lock);
        for (int j = 0; j < client_count; j++) {
            if (!should_ignore(rel_path, clients[j].ignore_list)) {
                send(clients[j].socket, header, strlen(header), 0);
                // Always send file content even if filesize is 0.
                if (filesize > 0) {
                    char *buf = malloc(filesize);
                    if (buf) {
                        fread(buf, 1, filesize, fp);
                        send(clients[j].socket, buf, filesize, 0);
                        free(buf);
                    }
                }
            }
        }
        pthread_mutex_unlock(&lock);
        fclose(fp);
    }
}

/* Broadcasts an update message to all connected clients.
   Message format: <command> <type> <relative_path>\n
   For file creation events (non-directory), file content is sent instead.
*/
void broadcast_update(const char *cmd, const char *rel_path, int is_dir) {
    char norm_rel[512];
    strncpy(norm_rel, rel_path, sizeof(norm_rel));
    norm_rel[sizeof(norm_rel)-1] = '\0';
    normalize_path(norm_rel);
    
    // For a MOVED_TO event on a directory coming in from outside, recursively scan.
    if (strcmp(cmd, "MOVED_TO") == 0 && is_dir) {
        char abs_path[512];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", base_directory, norm_rel);
        scan_and_broadcast_creation(abs_path, norm_rel);
        return;
    }
    
    pthread_mutex_lock(&lock);
    for (int j = 0; j < client_count; j++) {
        if (!should_ignore(norm_rel, clients[j].ignore_list)) {
            if (strcmp(cmd, "CREATE") == 0 && !is_dir) {
                char abs_path[512];
                snprintf(abs_path, sizeof(abs_path), "%s/%s", base_directory, norm_rel);
                FILE *fp = fopen(abs_path, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    long filesize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    char header[512];
                    snprintf(header, sizeof(header), "CREATE FILE %s %ld\n", norm_rel, filesize);
                    send(clients[j].socket, header, strlen(header), 0);
                    if (filesize > 0) {
                        char *buf = malloc(filesize);
                        if (buf) {
                            fread(buf, 1, filesize, fp);
                            send(clients[j].socket, buf, filesize, 0);
                            free(buf);
                        }
                    }
                    fclose(fp);
                    continue;
                }
            }
            char msg[512];
            const char *type_str = is_dir ? "DIR" : "FILE";
            snprintf(msg, sizeof(msg), "%s %s %s\n", cmd, type_str, norm_rel);
            send(clients[j].socket, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&lock);
}

/* inotify watcher thread: reads events, builds full relative paths using watch mappings, and broadcasts updates. */
void *watch_directory(void *arg) {
    char buffer[BUF_LEN];
    while (1) {
        int length = read(inotify_fd, buffer, BUF_LEN);
        if (length < 0)
            continue;
        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len) {
                char dir_rel[512] = "";
                for (int k = 0; k < mapping_count; k++) {
                    if (watch_mappings[k].wd == event->wd) {
                        strncpy(dir_rel, watch_mappings[k].rel_path, sizeof(dir_rel));
                        break;
                    }
                }
                char full_rel[512] = "";
                if (strlen(dir_rel) > 0)
                    snprintf(full_rel, sizeof(full_rel), "%s/%s", dir_rel, event->name);
                else
                    snprintf(full_rel, sizeof(full_rel), "%s", event->name);
                normalize_path(full_rel);
                
                const char *cmd = NULL;
                int is_dir = (event->mask & IN_ISDIR) ? 1 : 0;
                if ((event->mask & IN_CREATE) || ((event->mask & IN_CLOSE_WRITE) && !is_dir))
                    cmd = "CREATE";
                else if (event->mask & IN_DELETE)
                    cmd = "DELETE";
                else if (event->mask & IN_MOVED_FROM)
                    cmd = "MOVED_FROM";
                else if (event->mask & IN_MOVED_TO)
                    cmd = "MOVED_TO";
                else
                    cmd = "UNKNOWN";
                
                if ((event->mask & IN_CREATE) && is_dir) {
                    char new_abs[512];
                    snprintf(new_abs, sizeof(new_abs), "%s/%s", base_directory, full_rel);
                    add_watch_recursive(new_abs, full_rel);
                }
                broadcast_update(cmd, full_rel, is_dir);
            }
            i += EVENT_SIZE + event->len;
        }
    }
    return NULL;
}

/* Handles communication with a connected client.
   The client sends its ignore list upon connecting.
*/
void *handle_client(void *arg) {
    Client *client = (Client *)arg;
    char buffer[256];
    recv(client->socket, buffer, sizeof(buffer), 0);
    strncpy(client->ignore_list, buffer, sizeof(client->ignore_list)-1);
    client->ignore_list[sizeof(client->ignore_list)-1] = '\0';
    
    while (1) {
        int bytes = recv(client->socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0)
            break;
    }
    close(client->socket);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == client->socket) {
            for (int j = i; j < client_count-1; j++) {
                clients[j] = clients[j+1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* Main server function.
   Usage: ./server <sync_directory> <port> <max_clients>
*/
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <sync_directory> <port> <max_clients>\n", argv[0]);
        exit(1);
    }
    char *sync_dir = argv[1];
    int port = atoi(argv[2]);
    int max_clients = atoi(argv[3]);
    
    strncpy(base_directory, sync_dir, sizeof(base_directory)-1);
    base_directory[sizeof(base_directory)-1] = '\0';
    
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init");
        exit(1);
    }
    add_watch_recursive(base_directory, "");
    
    pthread_t watcher_thread;
    pthread_create(&watcher_thread, NULL, watch_directory, NULL);
    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_socket, max_clients) < 0) {
        perror("listen");
        exit(1);
    }
    printf("Server listening on port %d...\n", port);
    
    while (1) {
        int client_sock = accept(server_socket, NULL, NULL);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }
        pthread_mutex_lock(&lock);
        if (client_count >= max_clients) {
            pthread_mutex_unlock(&lock);
            close(client_sock);
            continue;
        }
        clients[client_count].socket = client_sock;
        client_count++;
        pthread_mutex_unlock(&lock);
        
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client, &clients[client_count-1]);
    }
    
    close(server_socket);
    return 0;
}
