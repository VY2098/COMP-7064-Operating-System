#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/select.h>

#define MAX_CLIENTS 50
#define MAX_DATA_LEN 1024

// Define a structure for the shared list node
struct Node {
    char *data;
    char *book_title;   //store the book title the node belongs to          
    struct Node *next;
    struct Node *book_next;            
    int searched;       //indicate searched or not yet
};
struct Node *shared_list = NULL;

// Define a structure for a client's header
struct ClientBook {
    struct Node *head;
};
struct ClientBook books[MAX_CLIENTS];

// Define a structure to store frequency data for each book
struct BookFrequency {
    char *title;
    int frequency;
};
struct BookFrequency bookFrequencyArray[MAX_CLIENTS];

int compareFrequency(const void *a, const void *b) {    //to sort the books
    const struct BookFrequency *bookA = (const struct BookFrequency *)a;
    const struct BookFrequency *bookB = (const struct BookFrequency *)b;
    return bookB->frequency - bookA->frequency;
}

pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t client_CV = PTHREAD_COND_INITIALIZER;
pthread_cond_t analysis_CV = PTHREAD_COND_INITIALIZER;
pthread_mutex_t frequency_mutex = PTHREAD_MUTEX_INITIALIZER;

time_t lastOutputTime = 0;
int n = 20;
int in = 0;
int out = 0;

struct ClientInfo {
    int client_socket;
    int client_order;
};

//function to count the frequency of pattern in string from the data
int countSubstr(const char *mainStr, const char *substr) {
    int count = 0;
    size_t len = strlen(substr);
    const char *pos = mainStr;

    while ((pos = strstr(pos, substr)) != NULL) {
        count++;
        pos += len;
    }

    return count;
}

void addNode(char *data, int client_order) {
    /*Each client shall add nodes to the shared linked list
    each added nodes shall record its corresponding book title and book_next node
    the book title is for ease of counting the pattern later
    book_next node is for ease of writing into txt file later*/
    struct Node *newNode = (struct Node *)malloc(sizeof(struct Node));
    newNode->data = strdup(data);
    newNode->next = NULL;
    newNode->book_title = NULL;
    newNode->book_next = NULL;
    newNode->searched = 0;

    pthread_mutex_lock(&list_mutex);
    while ((in+1) % n == out){          //referenced from the tutorial's producer/consumer CV codes
        pthread_cond_wait(&analysis_CV, &list_mutex);
    }
    if (shared_list == NULL) {
        shared_list = newNode;
    } else {
        struct Node *current = shared_list;
        while (current->next != NULL) {
            current = current->next; 
        }
        current->next = newNode;    // Add to shared linked list
    }

    if (books[client_order-1].head == NULL) {
        books[client_order-1].head = newNode;                       //start new head for the new book
        newNode->book_title = strdup(data);                         // Copy the book title to the newNode
        bookFrequencyArray[client_order-1].title = strdup(data);    //add the new book title into bookfrequency
        bookFrequencyArray[client_order-1].frequency = 0;           //initializev frequency as 0
    } else {
        struct Node *current = books[client_order-1].head;
        while (current->book_next != NULL) {
            current = current->book_next;   
        }
        current->book_next = newNode;                                       // Update the book next pointer
        newNode->book_title = strdup(books[client_order-1].head->data);     // Copy the book title to the newNode
    }

    in = (in + 1) % n ;
    pthread_cond_signal(&client_CV);    //reason for mutex_lock to cover so many lines of code as the analysis code also check the book title
    pthread_mutex_unlock(&list_mutex);
    
    // Print a report to stdout
    fprintf(stdout, "Added node to shared link list: from connection %d\n", client_order);

}

void writeBookToFile(struct Node *book, int client_order) {
    if (book == NULL) return;

    char filename[50];
    snprintf(filename, sizeof(filename), "book_%02d.txt", client_order);
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error creating book file");
        return;
    }

    struct Node *current = book;
    while (current != NULL) {
        fprintf(file, "%s", current->data);
        current = current->book_next;
    }
    fprintf(stdout, "book_%02d.txt saved\n", client_order);

    fclose(file);
}

void *clientHandler(void *socket_desc) {
    struct ClientInfo *client_info = (struct ClientInfo *)socket_desc;
    int client_socket = client_info->client_socket;
    char client_data[MAX_DATA_LEN];

    while (1) {
        //non-blocking reads
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        int select_result = select(client_socket + 1, &read_fds, NULL, NULL, &timeout);

        if (select_result == -1) {
            perror("Error in select");
            break;
        }

        if (select_result == 0) {
            continue;
        }

        ssize_t bytes_read = recv(client_socket, client_data, sizeof(client_data), 0);

        if (bytes_read <= 0) {
            break;
        }

        client_data[bytes_read] = '\0';

        // Append data to the shared list and the client's book
        addNode(client_data, client_info->client_order);
    }

    // Write the client's book to a file
    writeBookToFile(books[client_info->client_order-1].head, client_info->client_order);

    // Cleanup and close resources
    close(client_socket);
    free(client_info);

    pthread_exit(NULL);
}

// Define the analysis thread function
void *analysisThread(void *pattern) {
    while (1) {

        // Wait for data to be added to the linked list
        pthread_mutex_lock(&list_mutex);
        while (in == out) {
            fprintf(stdout, "Analysis thread go to sleep.\n");
            pthread_cond_wait(&client_CV, &list_mutex);
        }
        
        // Search the shared data structure for the given pattern
        struct Node *current = shared_list;
        while (current != NULL) {
            if (current->searched) {    //check whether searched by other thread
                current = current->next;
                continue; // Skip this node
            }

            // Process the current node
            const char *data = current->data;
            const char *title = current->book_title;

            int count = countSubstr(data, pattern);
        
            // If the pattern was found in the current string, update the frequency for the book
            if (count > 0) {
                // Lock the frequency data mutex to avoid race conditions
                pthread_mutex_lock(&frequency_mutex);

                // Find the book title in the frequency array
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (bookFrequencyArray[i].title == NULL || title == NULL) {
                        // Reached the end of valid entries, break
                        break;
                    }
                    if (strcmp(bookFrequencyArray[i].title, title) == 0) {
                            // Update the frequency count for the book
                            bookFrequencyArray[i].frequency += count;
                            break;
                    }
                }
                // Unlock the frequency data mutex
                pthread_mutex_unlock(&frequency_mutex);
                }

            current->searched = 1;  //mark node as searched so other anaylsis thread won't access
            out = (out + 1) % n;    //increment of count as 1 node is searched
            pthread_cond_signal(&analysis_CV);
            pthread_mutex_unlock(&list_mutex);
            
            if (current->next != NULL) {
                current = current->next;
            } else {
                // Handle the case where current->next is NULL, no more node to analyse
                //printf("Analysis thread sleep for 2 seconds.\n");
                sleep(2); // Sleep for 2 seconds before checking again
            }

        }
    }
}

// Function to print the frequency data
void *printFrequency(void *arg) {
    while (1) {
        pthread_mutex_lock(&frequency_mutex);
        // Output the sorted book titles
        qsort(bookFrequencyArray, MAX_CLIENTS, sizeof(struct BookFrequency), compareFrequency);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (bookFrequencyArray[i].frequency > 0) {
                fprintf(stdout, "Book Title: %sFrequency: %d\n", bookFrequencyArray[i].title, bookFrequencyArray[i].frequency);
            } else {
                // Don't print anymore when frequency drops to zero
                break;
            }
        }
        pthread_mutex_unlock(&frequency_mutex);

        sleep(5); // Sleep for 5 seconds before printing again
    }
}


int main(int argc, char* argv[]) {
    int opt;
    int port;
    char* pattern;

    while ((opt = getopt(argc, argv, "l:p:")) != -1) {
        switch (opt) {
        case 'l':
            port = atoi(optarg);
            break;
        case 'p':
            pattern = strdup(optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s -l port -p pattern\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }


    for (int i = 0; i < MAX_CLIENTS; i++) {
        books[i].head = NULL;
        }

    // Initialize the book frequency array elements
    for (int i = 0; i < MAX_CLIENTS; i++) {
        bookFrequencyArray[i].title = NULL; 
        bookFrequencyArray[i].frequency = 0;
    };

    // Create 2 analysis threads
    pthread_t analysis_threads[2];
    for (int i = 0; i < 2; i++) 
    {
        pthread_create(&analysis_threads[i], NULL, analysisThread, pattern);
    }

    //Create print thread
    pthread_t printFrequencyThread;
    pthread_create(&printFrequencyThread, NULL, printFrequency, NULL);


    int serv_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_socket == -1) {
        perror("Error creating server socket");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serv_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("Error binding socket");
        close(serv_socket);
        exit(1);
    }

    if (listen(serv_socket, 5) == -1) {
        perror("Error listening");
        close(serv_socket);
        exit(1);
    }

    fprintf(stdout, "Server listening on port %d...\n", port);

    int client_order = 0;

    while (1) {
        int *client_socket = (int *)malloc(sizeof(int));
        *client_socket = accept(serv_socket, NULL, NULL);
        if (*client_socket == -1) {
            perror("Error accepting connection");
            close(serv_socket);
            exit(1);
        }

        struct ClientInfo *client_info = (struct ClientInfo *)malloc(sizeof(struct ClientInfo));
        client_info->client_socket = *client_socket;
        client_info->client_order = ++client_order;

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, clientHandler, (void *)client_info) != 0) {
            perror("Error creating thread");
            close(serv_socket);
            free(client_socket);
            free(client_info);
            exit(1);
        }

        pthread_detach(client_thread);
    }

    close(serv_socket);

    pthread_exit(NULL);

    return 0;
}
