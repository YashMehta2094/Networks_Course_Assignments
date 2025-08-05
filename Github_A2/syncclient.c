#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

//RUN: ./syncclient <client_dir> <ignore_list> <server_ip> <server_port>
//Compile: gcc syncclient.c -o syncclient 
//Ignore List Format Example: ".mp4,.zip"

#define BUFFER_SIZE 512

int client_socket;
char sync_directory[512];
char old_filename[256] = "";  // For handling move operations
time_t moved_from_time = 0;     // Timestamp for MOVED_FROM events
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // Global lock

/* Recursively removes a directory and its contents. */
void remove_dir_recursive(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *entry;
    char full_path[512];
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                remove_dir_recursive(full_path);
            else
                remove(full_path);
        }
    }
    closedir(d);
    rmdir(dir_path);
}

/* Removes trailing slashes from a path. */
void normalize_path(char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len-1] == '/') {
        path[len-1] = '\0';
        len--;
    }
}

/* Reads the ignore list file and sends it to the server. */
void send_ignore_list(const char *ignore_file) {
    FILE *file = fopen(ignore_file, "r");
    if (!file) {
        perror("fopen");
        exit(1);
    }
    char ignore_list[256] = "";
    char extension[50];
    while (fscanf(file, "%49s", extension) == 1) {
        if (ignore_list[0] != '\0')
            strcat(ignore_list, ",");
        strcat(ignore_list, extension);
    }
    fclose(file);
    send(client_socket, ignore_list, strlen(ignore_list), 0);
}

/* Ensures that the directory structure exists for the given filepath. */
void ensure_directory_exists(const char *filepath) {
    char path[512];
    strcpy(path, filepath);
    char *p = strrchr(path, '/'); //Finds the last slash in the path
    if (p) {
        *p = '\0';
        mkdir(path, 0777);
    }
} //This is a simplified version. It doesn’t create full nested paths like mkdir -p, but it's used when the parent directory is assumed to already exist or be created before.

/* Monitor thread for MOVED_FROM events.
   If no MOVED_TO event is received within 1 second, deletes the directory.
*/
void *moved_from_monitor(void *arg) {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&lock);
        if (old_filename[0] != '\0' && (time(NULL) - moved_from_time) >= 1) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", sync_directory, old_filename);
            remove_dir_recursive(full_path);
            printf("Deleted (via MOVED_FROM timeout): %s\n", full_path);
            old_filename[0] = '\0';
            moved_from_time = 0;
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

/* Processes an update received from the server.
   Expected message formats:
     - For file creation: "CREATE FILE <relative_path> <filesize>\n" followed by file data.
     - For other events: "<command> <type> <relative_path>\n"
*/
void process_update(FILE *fp, const char *header) {
    char command[20], type[10], rel_path[256];
    if (strncmp(header, "CREATE FILE", 11) == 0) {
        long filesize = 0;
        if (sscanf(header, "%s %s %s %ld", command, type, rel_path, &filesize) != 4)
            return;
        normalize_path(rel_path);
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", sync_directory, rel_path);
        ensure_directory_exists(full_path);
        FILE *out = fopen(full_path, "wb");
        if (!out) return;
        if (filesize > 0) {
            char *buf = malloc(filesize);
            if (buf) {
                size_t total = 0;
                while (total < filesize) {
                    size_t r = fread(buf + total, 1, filesize - total, fp);
                    if (r <= 0) break;
                    total += r;
                }
                fwrite(buf, 1, total, out);
                free(buf);
            }
        }
        fclose(out);
        printf("File created: %s (size: %ld bytes)\n", full_path, filesize);
    } else {
        if (sscanf(header, "%s %s %s", command, type, rel_path) != 3)
            return;
        normalize_path(rel_path);
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", sync_directory, rel_path);
        if (strcmp(command, "CREATE") == 0) {
            if (strcmp(type, "DIR") == 0) {
                ensure_directory_exists(full_path);
                mkdir(full_path, 0777);
                printf("Directory created: %s\n", full_path);
            } else {
                ensure_directory_exists(full_path);
                FILE *fp_tmp = fopen(full_path, "w");
                if (fp_tmp) { fclose(fp_tmp); }
                printf("Empty file created: %s\n", full_path);
            }
        } else if (strcmp(command, "DELETE") == 0) {
            struct stat st;
            if (stat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode))
                    remove_dir_recursive(full_path);
                else
                    remove(full_path);
                printf("Deleted: %s\n", full_path);
            }
        } else if (strcmp(command, "MOVED_FROM") == 0) {
            strncpy(old_filename, rel_path, sizeof(old_filename)-1);
            old_filename[sizeof(old_filename)-1] = '\0';
            normalize_path(old_filename);
            moved_from_time = time(NULL);
        } else if (strcmp(command, "MOVED_TO") == 0) {
            if (old_filename[0] != '\0') {
                char old_full[512], new_full[512];
                snprintf(old_full, sizeof(old_full), "%s/%s", sync_directory, old_filename);
                snprintf(new_full, sizeof(new_full), "%s/%s", sync_directory, rel_path);
                ensure_directory_exists(new_full);
                rename(old_full, new_full);
                printf("Renamed: %s -> %s\n", old_full, new_full);
                old_filename[0] = '\0';
                moved_from_time = 0;
            } else {
                // Treat as creation event.
                if (strcmp(type, "DIR") == 0) {
                    ensure_directory_exists(full_path);
                    mkdir(full_path, 0777);
                    printf("Directory created (via MOVED_TO): %s\n", full_path);
                } else {
                    ensure_directory_exists(full_path);
                    FILE *fp_tmp = fopen(full_path, "w");
                    if (fp_tmp) { fclose(fp_tmp); }
                    printf("Empty file created (via MOVED_TO): %s\n", full_path);
                }
            }
        }
    }
}

/* Receives updates from the server and applies them.
   Uses a FILE stream (via fdopen) to support file content transfer.
*/
void *receive_updates(void *arg) {
    FILE *fp = fdopen(client_socket, "rb");
    if (!fp) {
        perror("fdopen");
        return NULL;
    }
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strncmp(buffer, "CREATE FILE", 11) == 0)
            process_update(fp, buffer);
        else {
            printf("Update: %s", buffer);
            process_update(fp, buffer);
        }
    }
    fclose(fp);
    return NULL;
}

/* Main function - connects to the server and starts receiving updates.
   Usage: ./client <sync_directory> <ignore_file> <server_ip> <port>
*/
int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <sync_directory> <ignore_file> <server_ip> <port>\n", argv[0]);
        return 1;
    }
    
    strncpy(sync_directory, argv[1], sizeof(sync_directory)-1);
    sync_directory[sizeof(sync_directory)-1] = '\0';
    char *ignore_file = argv[2];
    char *server_ip = argv[3];
    int port = atoi(argv[4]);
    
    struct sockaddr_in server_addr;
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("socket");
        return 1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
    
    // Send ignore list to the server.
    send_ignore_list(ignore_file);
    
    printf("Connected to server. Syncing directory: %s\n", sync_directory);
    
    pthread_t monitor_thread, update_thread;
    pthread_create(&monitor_thread, NULL, moved_from_monitor, &lock);
    pthread_create(&update_thread, NULL, receive_updates, NULL);
    pthread_join(update_thread, NULL);
    
    close(client_socket);
    return 0;
}
