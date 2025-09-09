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

void handleChatSession(int senderSocket, int receiverSocket) {
    char buffer[1024];
    while (true) {
        int valread = read(senderSocket, buffer, sizeof(buffer));
        if (valread <= 0) break;
        send(receiverSocket, buffer, valread, 0);
    }
}

void handleClient(int client_socket) {
    char buffer[1024] = {0};
    string name;

    // -------------------------
    // İsim kontrol döngüsü
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
            string msg = "Bu isim kullaniliyor, tekrar deneyin: ";
            send(client_socket, msg.c_str(), msg.size(), 0);
        } else {
            lock_guard<mutex> lock(clientsMtx);
            allClients[client_socket] = {client_socket, name, ""};
            string welcomeMsg = "Hoş geldin, " + name + "! Sohbet için '/chat <isim>' veya '/list' kullanabilirsiniz.";
            send(client_socket, welcomeMsg.c_str(), welcomeMsg.size(), 0);
            break;
        }
    }

    cout << "Yeni istemci bağlandi: " << name << endl;

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_socket, buffer, sizeof(buffer));
        if (valread <= 0) {
            cout << "İstemci " << name << " bağlantisi kapandi." << endl;
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

            // Kendisiyle sohbet etmeye çalişiyorsa
            if (targetName == name) {
                string errorMsg = "Kendinle sohbet edemezsin.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
                continue;
            }

            if (targetSocket != -1) {
                lock_guard<mutex> lock(chatMtx);
                allClients[client_socket].currentChatPartner = targetName;
                allClients[targetSocket].currentChatPartner = name;

                string startMsg = "Eşleşme bulundu! " + targetName + " ile sohbet ediyorsunuz.";
                send(client_socket, startMsg.c_str(), startMsg.size(), 0);
                startMsg = "Eşleşme bulundu! " + name + " ile sohbet ediyorsunuz.";
                send(targetSocket, startMsg.c_str(), startMsg.size(), 0);

                thread t1(handleChatSession, client_socket, targetSocket);
                thread t2(handleChatSession, targetSocket, client_socket);
                t1.join();
                t2.join();
            } else {
                string errorMsg = "Kullanici bulunamadi veya meşgul.";
                send(client_socket, errorMsg.c_str(), errorMsg.size(), 0);
            }
        } else {
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
        perror("Soket oluşturma hatasi");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Soket bağlama hatasi");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Dinleme hatasi");
        return 1;
    }

    cout << "Sunucu dinlemede... (port 8080)" << endl;

    while (true) {
        socklen_t addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket < 0) {
            perror("Bağlanti kabul etme hatasi");
            continue;
        }
        thread(handleClient, client_socket).detach();
    }

    close(server_fd);
    return 0;
}
