#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

const int BUFFER_SIZE = 1024;

void send_ok_response(int connection, char *message, int message_len);
void send_404(int connection);
void try_serve_file(int connection, char *file_name);
void parse_requested_file(char *request, char *file_name);
void parse_requested_file_type(char *requested_file, char *type);
bool string_contains_substring(char *str, char *sub);
void send_500(int connection);

void handle_connection(int connection);

int main(int argc, char *args[]){

    int socket_fd;
    struct sockaddr_in address;

    // 1. Create the socket
    if( (socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        perror("Can't create socket"); 
        return 1;
    }

    // NEW CODE: Forcefully attach the socket to the port
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        return 1;
    }

    // 2. Configure the address struct
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(8080);       // Set port to 8080 (converted to network byte order)

    // 3. Bind the socket to the address and port
    // We cast &address back to a generic (struct sockaddr *) for the function
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    // 4. Tell the socket to listen for incoming connections
    // The '10' is the backlog: how many connections can wait in line
    if (listen(socket_fd, 10) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server is actively listening on port 8080...\n");
    
    int connection;

    socklen_t addr_len = sizeof(address); 

    while(1){
        connection = accept(socket_fd, (struct sockaddr *)&address, &addr_len);

        if (connection < 0) {
            perror("Accept failed");
            continue;
        }

        int  pid = fork();

        if(pid < 0){
            send_500(connection);
            close(connection);
        }else if(pid == 0){
            handle_connection(connection);
        }else{
            close(connection);
        }

    }
 
    close(socket_fd);

    return 0;
}

void send_ok_response(int connection, char *message, int message_len){

    char http_response[BUFFER_SIZE];

    http_response[0] = '\0';

    char *http_okay_header = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 26\r\n"
            "\r\n";

    strcat(http_response, http_okay_header);
    strcat(http_response, message);

    send(connection, http_response, strlen(http_response), 0);
    
}

void send_404(int connection){
    char response[] = 
        "HTTP/1.1 404 Bad request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 26\r\n"
        "\r\n";

    send(connection, response, strlen(response), 0);


}

void send_500(int connection){
    char response[] = 
        "HTTP/1.1 500 Internal server error\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 26\r\n"
        "\r\n";

    send(connection, response, strlen(response), 0);

}

void try_serve_file(int connection, char *file_name){

    char request_file_type[101] = {'\0'};

    parse_requested_file_type(file_name, request_file_type);

    if(strcmp(request_file_type, "ERR") == 0){
        printf("Unsupported file type");
        send_500(connection);
        return;
    }

    printf("Requested file type: %s\n", request_file_type);

    FILE *send_file = NULL;

    char file_path[101] = "../www/";

    strcat(file_path, file_name);

    if( (send_file = fopen(file_path, "rb")) == NULL ){
        printf("Error trying to open: %s\n", file_path);
        send_404(connection);
        return;
    }

    fseek(send_file, 0, SEEK_END);     // Fast-forward to the end of the file
    long file_size = ftell(send_file); // Get the current byte offset (this is the file size)
    rewind(send_file);

    char *file_buff = malloc(file_size * sizeof(char));

    if( !file_buff ){
        send_500(connection);
    }

    fread( file_buff, 1, file_size, send_file );

    fclose(send_file);

    char header[1024] = {'\0'};

    sprintf(header, "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "\r\n", request_file_type, file_size);

    send(connection, header, strlen(header), 0);
    send(connection, file_buff, file_size, 0);

    printf("Serving file: %s\n", file_name);

    free(file_buff);

}

void parse_requested_file(char *request, char *file_name){

    char *start_of_name = strchr(request, '/') + 1;

    int count = (int) ( strchr(start_of_name, 32) - start_of_name );

    memcpy(file_name, strchr(request, '/') + 1, count);

}

void parse_requested_file_type(char *requested_file, char *type){

    strcpy(type, strchr(requested_file, '.'));

    char *scratch;

    scratch = strchr(type, '.');

    while( scratch != NULL ){
        strcpy(type, scratch);

        scratch = strchr(type + 1, '.');        
    }

    if(strcmp(type, ".html") == 0){
        strcpy(type, "text/html");
    }else if(strcmp(type, ".png") == 0){
        strcpy(type, "image/png");
    }else if(strcmp(type, ".css") == 0){
        strcpy(type, "text/css");
    }else{
        strcpy(type, "ERR");
    }

}

bool string_contains_substring(char *str, char *sub){

    int str_len = strlen(str);
    int sub_len = strlen(sub);

    // An empty substring is technically always found
    if (sub_len == 0) return true;

    // We only loop as long as there is enough room left in 'str' for 'sub'
    for (int i = 0; i <= str_len - sub_len; i++) {
        bool match = true;

        for (int j = 0; j < sub_len; j++) {
            if (str[i + j] != sub[j]) {
                match = false;
                break; // Mismatch found, break inner loop and try next 'i'
            }
        }

        if (match) return true; // We made it through the whole inner loop!
    }

    return false;
}


void handle_connection(int connection){

    char buffer[BUFFER_SIZE];

    read(connection, buffer, BUFFER_SIZE - 1);

    char request_file[101] = { '\0' };

    parse_requested_file(buffer, request_file);

    printf("Requested file: %s\n", request_file);

    if(strcmp(request_file, "") == 0){
        strcpy(request_file, "index.html");
    }

    if(string_contains_substring(request_file, "..")){
        send_404(connection);
        close(connection);
        return;
    }

    try_serve_file(connection, request_file);

    close(connection);
}