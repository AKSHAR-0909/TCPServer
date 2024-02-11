#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unistd.h>

//TEST WORKFLOWS 2

using namespace std;

pthread_mutex_t dbMutex;
pthread_mutex_t qMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

const int threadCount = 10;
queue<int> clients;

class Database {
    private:
    unordered_map<string, string> db;

    public:
    string write(string key, string value) {
        pthread_mutex_lock(&dbMutex);
        db[key] = value.substr(1);
        pthread_mutex_unlock(&dbMutex);
        return "FIN";
    }

    string read(string key) {
        unordered_map<string, string>::iterator iter = db.find(key);
        if (iter == db.end()) return "NULL";
        return iter->second;
    }

    int count() {
        return db.size();
    }

    string remove(string key) {
        pthread_mutex_lock(&dbMutex);
        bool done = db.erase(key);
        pthread_mutex_unlock(&dbMutex);
        if (done) return "FIN";
        return "NULL";
    }
};

Database db;

int CreateSocket(int portno) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        // perror("socket");
        return -1;
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(portno);

    if (bind(sfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        // perror("bind");
        close(sfd);
        return -1;
    }

    if (listen(sfd, 5) == -1) {
        // perror("listen");
        close(sfd);
        return -1;
    }

    return sfd;
}

int AcceptConnection(int serverSocket) {
    struct sockaddr_in clientAddress;
    socklen_t clientAddressLen = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddressLen);
    if (clientSocket == -1) {
        // perror("accept");
        return -1;
    }
    return clientSocket;
}

void* HandleClient(void* arg) {
    int clientSocket = *((int*)arg);
    char buffer[1024];
    ssize_t bytesReceived;

    bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (bytesReceived == -1) {
        // perror("recv");
        close(clientSocket);
        return NULL;
    }

    string temp = "";
    vector<string> msg;
    for (int i = 0; i < bytesReceived; i++) {
        if (buffer[i] != '\n') temp += buffer[i];
        else {
            msg.push_back(temp);
            temp = "";
        }
    }

    int i = 0;
    int len = msg.size();
    while (i < len) {
        if (msg[i] == "COUNT") {
            string count = to_string(db.count()) + "\n";
            send(clientSocket, count.c_str(), count.size(), 0);
        }
        if (msg[i] == "READ") {
            string key = (i + 1 < len) ? msg[++i] : "NULL";
            string value = db.read(key) + "\n";
            send(clientSocket, value.c_str(), value.size(), 0);
        }
        if (msg[i] == "WRITE") {
            string key = (i + 1 < len) ? msg[++i] : "NULL";
            string value = (i + 1 < len) ? msg[++i] : "NULL";
            string result = db.write(key, value) + "\n";
            send(clientSocket, result.c_str(), result.size(), 0);
        }
        if (msg[i] == "DELETE") {
            string key = (i + 1 < len) ? msg[++i] : "NULL";
            string result = db.remove(key) + "\n";
            send(clientSocket, result.c_str(), result.size(), 0);
        }
        if (msg[i] == "END") break;

        i++;
    }
    string result = "\n";
    send(clientSocket, result.c_str(), result.size(), 0);
    close(clientSocket); // Close client socket
    return NULL;
}

void* HandleQueue(void* arg) {
    while (1) {
        pthread_mutex_lock(&qMutex);
        int clientSocket;
        if (clients.empty()) {
            pthread_cond_wait(&cv, &qMutex);
            if (!clients.empty()) {
                clientSocket = clients.front();
                clients.pop();
            }
        }
        pthread_mutex_unlock(&qMutex);
        HandleClient(&clientSocket);
    }
}

void* AcceptConnections(void* arg) {
    int serverSocket = *((int*)arg);
    while (1) {
        int clientSocket = AcceptConnection(serverSocket);
        pthread_mutex_lock(&qMutex);
        clients.push(clientSocket);
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&qMutex);
    }   
}

int main(int argc, char** argv) {
    int portno;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    portno = atoi(argv[1]);

    pthread_t threadPool[threadCount];

    for (int i = 0; i < threadCount; i++) {
        pthread_create(&threadPool[i], NULL, HandleQueue, NULL);
    }

    int serverSocket = CreateSocket(portno);

    pthread_t acceptThread;
    pthread_create(&acceptThread, NULL, AcceptConnections, (void*)&serverSocket);

    // for (int i = 0; i < threadCount; i++) {
    //     pthread_join(threadPool[i], NULL);
    // }

    pthread_join(acceptThread, NULL);

    close(serverSocket);

    return 0;
}