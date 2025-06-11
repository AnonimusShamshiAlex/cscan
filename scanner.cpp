#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

std::mutex queueMutex;
std::condition_variable cv;
std::queue<std::pair<std::string, int>> taskQueue;
bool stop = false;

// Цвета терминала
const std::string GREEN = "\033[1;32m";
const std::string RED = "\033[1;31m";
const std::string RESET = "\033[0m";

// Сканирование одного порта
void scanPort(const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Ошибка создания сокета для " << ip << ":" << port << "\n";
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        std::cout << GREEN << "Открытый порт найден: " << ip << ":" << port << RESET << "\n" << std::flush;
    } else {
        std::cout << RED << "Неудачная попытка: " << ip << ":" << port << RESET << "\n" << std::flush;
    }

    close(sock);
}

// Рабочий поток
void worker() {
    while (true) {
        std::pair<std::string, int> task;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [] { return !taskQueue.empty() || stop; });

            if (stop && taskQueue.empty()) break;

            task = taskQueue.front();
            taskQueue.pop();
        }

        scanPort(task.first, task.second);
    }
}

// Генерация IP-адресов в подсети
std::vector<std::string> generateIPs(const std::string& baseIP) {
    std::vector<std::string> ips;
    for (int i = 1; i <= 254; ++i) {
        std::ostringstream oss;
        oss << baseIP << "." << i;
        ips.push_back(oss.str());
    }
    return ips;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Использование: " << argv[0] << " <Базовый IP-адрес>\n";
        std::cerr << "Пример: " << argv[0] << " 192.168.0\n";
        return 1;
    }

    std::string baseIP = argv[1];

    // 🆕 Порты: добавлены IoT — Tuya, Yeelight, ESPHome и пр.
    std::vector<int> portsToCheck = {
        21, 22, 23, 80, 443, 554, 8080, 3306, 3389, 5900, 9100,
        6666, 6668, 55443, 8888, 5353, 1982, 5683
    };

    auto ips = generateIPs(baseIP);

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        for (const auto& ip : ips) {
            for (int port : portsToCheck) {
                taskQueue.emplace(ip, port);
            }
        }
    }

    std::cout << "Количество задач в очереди: " << taskQueue.size() << "\n";

    int threadCount = 16;
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stop = true;
    }
    cv.notify_all();

    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "Сканирование завершено.\n";
    return 0;
}
