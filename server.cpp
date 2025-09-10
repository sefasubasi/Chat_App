#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <mutex>
#include <map>

using namespace std;

struct Client {
    int socket;
    string name;
    string currentChatPartner;
};

map<int, Client> allClients;
mutex clientsMtx;
mutex chatMtx;

void sendClientList(int client_socket) {
    lock_guard<mutex> lock(clientsMtx);
    string list = "/list";
    for (const auto& pair : allClients) {
        list += " " + pair.second.name;
    }
    send(client_socket, list.c_str(), list.size(), 0);
}

//File transfer function
void forwardFile(int senderSocket, int receiverSocket, const string& filename, size_t filesize) {
    //Send file information to the recipient first
    string header = "/file " + filename + " " + to_string(filesize);
    send(receiverSocket, header.c_str(), header.size(), 0);

    char buffer[4096];
    size_t totalRead = 0;

    while (totalRead < filesize) {
        ssize_t bytesRead = read(senderSocket, buffer, min(sizeof(buffer), filesize - totalRead));
        if (bytesRead <= 0) break;
        send(receiverSocket, buffer, bytesRead, 0);
        totalRead += bytesRead;
    }

    cout << "File transfer complete: " << filename << " (" << filesize << " bytes)" << endl;
}

void handleClient(int client_socket) {
    char buffer[1024] = {0};
    string name;

    // -------------------------
    // Name check loop
    // -------------------------
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_socket, buffer, sizeof(buffer));
        if (valread <= 0) {
            close(client_socket);
            return;
        }

        name = string(buffer, valread);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
            name.pop_back();
        }

        bool nameExists = false;
        {
            lock_guard<mutex> lock(clientsMtx);
            for (const auto& pair : allClients) {
                if (pair.second.name == name) {
                    nameExists = true;
                    break;
                }
            }
        }

        if (nameExists) {
            string msg = "This name is already in use, please try again: ";
            send(client_socket, msg.c_str(), msg.size(), 0);
        } else {
            lock_guard<mutex> lock(clientsMtx);
            allClients[client_socket] = {client_socket, name, ""};
            string welcomeMsg = "Welcome, " + name + "! Use '/chat <name>' or '/list' to chat. Use '/file <filename>' to send a file.";
            send(client_socket, welcomeMsg.c_str(), welcomeMsg.size(), 0);
            break;
        }
    }

    cout << "New client connected: " << name << endl;

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_socket, buffer, sizeof(buffer));
        if (valread <= 0) {
            cout << "Client " << name << " connection closed." << endl;
            {
                lock_guard<mutex> lock(clientsMtx);
                allClients.erase(client_socket);
            }
            close(client_socket);
            break;
        }

        string message(buffer, valread);

        if (message.rfind("/list", 0) == 0) {
            sendClientList(client_socket);
        } 
        else if (message.rfind("/chat ", 0) == 0) {
            string targetName = message.substr(6);
            int targetSocket = -1;
            {
                lock_guard<mutex> lock(clientsMtx);
                for (const auto& pair : allClients) {
                    if (pair.second.name == targetName) {
                        targetSocket = pair.first;
                        break;
                    }
                }
            }

            if (targetName == name) {
                string errorMsg = "You cannot chat with yourself.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
                continue;
            }

            if (targetSocket != -1) {
                lock_guard<mutex> lock(chatMtx);
                allClients[client_socket].currentChatPartner = targetName;
                allClients[targetSocket].currentChatPartner = name;

                string startMsg = "Match found! You are now chatting with " + targetName + ".";
                send(client_socket, startMsg.c_str(), startMsg.size(), 0);
                startMsg = "Match found! You are now chatting with " + name + ".";
                send(targetSocket, startMsg.c_str(), startMsg.size(), 0);
            } else {
                string errorMsg = "User not found or busy.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
            }
        }
        else if (message.rfind("/file ", 0) == 0) {
            //Expected format: "/file filename size"
            string rest = message.substr(6);
            size_t spacePos = rest.find(' ');
            if (spacePos == string::npos) {
                string errorMsg = "Invalid file command.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
                continue;
            }

            string filename = rest.substr(0, spacePos);
            size_t filesize = stoul(rest.substr(spacePos + 1));

            lock_guard<mutex> lock(clientsMtx);
            string partnerName = allClients[client_socket].currentChatPartner;
            if (partnerName.empty()) {
                string errorMsg = "You are not in a chat session.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
                continue;
            }

            int partnerSocket = -1;
            for (const auto& pair : allClients) {
                if (pair.second.name == partnerName) {
                    partnerSocket = pair.first;
                    break;
                }
            }

            if (partnerSocket != -1) {
                forwardFile(client_socket, partnerSocket, filename, filesize);
            } else {
                string errorMsg = "Partner not found.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
            }
        }
        else {
            lock_guard<mutex> lock(clientsMtx);
            string partnerName = allClients[client_socket].currentChatPartner;
            if (!partnerName.empty()) {
                int partnerSocket = -1;
                for (const auto& pair : allClients) {
                    if (pair.second.name == partnerName) {
                        partnerSocket = pair.first;
                        break;
                    }
                }
                if (partnerSocket != -1) {
                    string formattedMsg = name + ": " + message;
                    send(partnerSocket, formattedMsg.c_str(), formattedMsg.size(), 0);
                }
            }
        }
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket creation error");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Socket binding error");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen error");
        return 1;
    }

    cout << "Server is listening on port 8080..." << endl;

    while (true) {
        socklen_t addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket < 0) {
            perror("Accept error");
            continue;
        }
        thread(handleClient, client_socket).detach();
    }

    close(server_fd);
    return 0;
}
