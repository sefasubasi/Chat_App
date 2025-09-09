#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

using namespace std;

void receiveMessages(int sock) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread <= 0) {
            cout << "Sunucu bağlantisi kapandi." << endl;
            close(sock);
            exit(0);
        }
        string message(buffer, valread);
        if (message.rfind("/list", 0) == 0) {
            cout << "\n--- Aktif Kullanicilar ---" << endl;
            cout << message.substr(5) << endl;
            cout << "-------------------------" << endl;
        } else {
            cout << "\n>> " << message << endl;
        }
        cout << "Ben: ";
        cout.flush();
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Soket oluşturma hatasi");
        return 1;
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Geçersiz adres");
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bağlanti hatasi");
        return 1;
    }

    cout << "Sunucuya bağlanildi!" << endl;

    string name;
    while (true) {
        cout << "İsminizi girin: ";
        getline(cin, name);
        send(sock, name.c_str(), name.size(), 0);

        char buffer[1024] = {0};
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread > 0) {
            string response(buffer, valread);
            if (response.find("Bu isim kullaniliyor") != string::npos)//npos pozisyon bulunamadı anlamına gelir eğer belitrilen string bulunmuyorsa farklıdır ve else geçer.
            {
                cout << response << endl; // tekrar isim sor
            } else 
            {
                cout << response << endl; // Hoş geldin mesaji
                break;
            }
        }
    }

    thread receiver(receiveMessages, sock);
    receiver.detach();

    string msg;
    while (true) {
        cout << "Ben: ";
        getline(cin, msg);
        if (msg == "/quit") {
             cout << "Çikiş yapiliyor..." << endl;
                break;
            }
        send(sock, msg.c_str(), msg.size(), 0);
    }

    close(sock);
    return 0;
}