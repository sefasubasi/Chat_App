#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

using namespace std;

int globalSock; //So that the receiver thread can access

void receiveMessages(int sock) {
    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread <= 0) {
            cout << "Server connection closed." << endl;
            close(sock);
            exit(0);
        }

        string message(buffer, valread);

        if (message.rfind("/list", 0) == 0) {
            cout << "\n--- Active Users ---" << endl;
            cout << message.substr(5) << endl;
            cout << "-------------------------" << endl;
        } 
        else if (message.rfind("/file ", 0) == 0) {
            //Format: /file filename size
            string rest = message.substr(6);
            size_t spacePos = rest.find(' ');
            if (spacePos == string::npos) continue;

            string filename = rest.substr(0, spacePos);
            size_t filesize = stoul(rest.substr(spacePos + 1));

            cout << "\nIncoming file: " << filename << " (" << filesize << " bytes)" << endl;

            string saveName = "received_" + filename;
            ofstream outFile(saveName, ios::binary);

            size_t totalRead = 0;
            while (totalRead < filesize) {
                int bytesRead = read(sock, buffer, min(sizeof(buffer), filesize - totalRead));
                if (bytesRead <= 0) break;
                outFile.write(buffer, bytesRead);
                totalRead += bytesRead;
            }

            outFile.close();
            cout << "File saved as " << saveName << endl;
        }
        else {
            cout << "\n>> " << message << endl;
        }

        cout << "Me: ";
        cout.flush();
    }
}

void sendFile(int sock, const string& filename) {
    ifstream inFile(filename, ios::binary | ios::ate);
    if (!inFile) {
        cout << "File not found: " << filename << endl;
        return;
    }

    size_t filesize = inFile.tellg();
    inFile.seekg(0);

    //Send header first
    string header = "/file " + filename + " " + to_string(filesize);
    send(sock, header.c_str(), header.size(), 0);

    char buffer[4096];
    size_t totalSent = 0;

    while (!inFile.eof()) {
        inFile.read(buffer, sizeof(buffer));
        streamsize bytesRead = inFile.gcount();
        if (bytesRead > 0) {
            send(sock, buffer, bytesRead, 0);
            totalSent += bytesRead;
        }
    }

    inFile.close();
    cout << "File sent: " << filename << " (" << totalSent << " bytes)" << endl;
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return 1;
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return 1;
    }

    cout << "Connected to the server!" << endl;

    string name;
    while (true) {
        cout << "Enter your name: ";
        getline(cin, name);
        send(sock, name.c_str(), name.size(), 0);

        char buffer[1024] = {0};
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread > 0) {
            string response(buffer, valread);
            if (response.find("This name is already in use") != string::npos) {
                cout << response << endl;
            } else {
                cout << response << endl;
                break;
            }
        }
    }

    globalSock = sock;
    thread receiver(receiveMessages, sock);
    receiver.detach();

    string msg;
    while (true) {
        cout << "Me: ";
        getline(cin, msg);

        if (msg == "/quit") {
            cout << "Exiting..." << endl;
            break;
        }
        else if (msg.rfind("/file ", 0) == 0) {
            string filename = msg.substr(6);
            sendFile(sock, filename);
        }
        else {
            send(sock, msg.c_str(), msg.size(), 0);
        }
    }

    close(sock);
    return 0;
}
