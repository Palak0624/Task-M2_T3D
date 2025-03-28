#include <iostream>
#include <fstream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Color codes for terminal output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"

struct TrafficData {
    time_t timestamp;
    int trafficLightId;
    int carsPassed;

    TrafficData(time_t ts, int id, int cars)
        : timestamp(ts), trafficLightId(id), carsPassed(cars) {}
};

struct CompareCars {
    bool operator()(const TrafficData& a, const TrafficData& b) {
        return a.carsPassed > b.carsPassed;
    }
};

// Shared data
std::queue<TrafficData> trafficQueue;
std::mutex queueMutex;
std::condition_variable queueCV;
bool producersDone = false;

// Top N congested signals
std::priority_queue<TrafficData, std::vector<TrafficData>, CompareCars> topNQueue;
std::mutex topNMutex;
const int N = 3;

// Total cars per signal
std::unordered_map<int, int> totalCarsPerSignal;
std::mutex totalCarsMutex;

time_t parseTime(const std::string& timeStr) {
    std::tm tm = {};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        throw std::runtime_error("Failed to parse time: " + timeStr);
    }
    return std::mktime(&tm);
}

std::string formatTime(time_t timestamp) {
    std::tm tm;
    localtime_s(&tm, &timestamp);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buffer);
}

void printSeparator() {
    std::cout << COLOR_CYAN << "----------------------------------------" << COLOR_RESET << std::endl;
}

void producer(const std::string& filePath, int producerId) {
    try {
        std::ifstream file(filePath);
        std::string line;

        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string timestampStr;
            int id, cars;
            char comma;

            if (std::getline(iss, timestampStr, ',') && 
                (iss >> id >> comma >> cars)) {
                
                time_t timestamp = parseTime(timestampStr);
                TrafficData data(timestamp, id, cars);

                std::unique_lock<std::mutex> lock(queueMutex);
                trafficQueue.push(data);
                lock.unlock();
                queueCV.notify_one();

                // Color-coded output
                std::cout << COLOR_GREEN << "[" << timestampStr << "]" 
                          << COLOR_RESET << " Traffic Light " << COLOR_YELLOW << id 
                          << COLOR_RESET << ": " << COLOR_BLUE << cars 
                          << COLOR_RESET << " cars passed." << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in producer " << producerId << ": " << e.what() << "\n";
    }

    if (producerId == 0) {
        std::unique_lock<std::mutex> lock(queueMutex);
        producersDone = true;
        lock.unlock();
        queueCV.notify_all();
    }
}

void consumer(int consumerId) {
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, [] { return !trafficQueue.empty() || producersDone; });

        if (trafficQueue.empty() && producersDone) break;

        if (!trafficQueue.empty()) {
            TrafficData data = trafficQueue.front();
            trafficQueue.pop();
            lock.unlock();

            {
                std::lock_guard<std::mutex> lock(totalCarsMutex);
                totalCarsPerSignal[data.trafficLightId] += data.carsPassed;
            }

            {
                std::lock_guard<std::mutex> lock(topNMutex);
                if (topNQueue.size() < N) {
                    topNQueue.push(data);
                } else if (data.carsPassed > topNQueue.top().carsPassed) {
                    topNQueue.pop();
                    topNQueue.push(data);
                }
            }
        }
    }
}

void printTopN() {
    std::vector<TrafficData> topNList;
    {
        std::lock_guard<std::mutex> lock(topNMutex);
        while (!topNQueue.empty()) {
            topNList.push_back(topNQueue.top());
            topNQueue.pop();
        }
    }

    std::sort(topNList.begin(), topNList.end(), 
        [](const TrafficData& a, const TrafficData& b) {
            return a.carsPassed > b.carsPassed;
        });

    printSeparator();
    std::cout << COLOR_MAGENTA << "TOP " << N << " MOST CONGESTED SIGNALS" << COLOR_RESET << std::endl;
    for (const auto& data : topNList) {
        std::cout << "  - Signal " << COLOR_YELLOW << std::setw(2) << data.trafficLightId << COLOR_RESET
                  << ": " << COLOR_RED << std::setw(3) << data.carsPassed << COLOR_RESET 
                  << " cars at " << COLOR_GREEN << formatTime(data.timestamp) << COLOR_RESET << std::endl;
    }
}

void printTotalCars() {
    printSeparator();
    std::cout << COLOR_MAGENTA << "TOTAL CARS PER TRAFFIC SIGNAL" << COLOR_RESET << std::endl;
    for (const auto& [id, total] : totalCarsPerSignal) {
        std::cout << "  - Signal " << COLOR_YELLOW << std::setw(2) << id << COLOR_RESET
                  << ": " << COLOR_BLUE << std::setw(4) << total << COLOR_RESET << " cars" << std::endl;
    }
}

int main() {
    const std::string dataFile = "traffic_data.txt";
    const int numProducers = 1;
    const int numConsumers = 2;

    std::cout << COLOR_CYAN << "TRAFFIC CONTROL SIMULATION STARTED" << COLOR_RESET << std::endl;
    printSeparator();

    std::vector<std::thread> producers, consumers;
    for (int i = 0; i < numProducers; ++i) {
        producers.emplace_back(producer, dataFile, i);
    }
    for (int i = 0; i < numConsumers; ++i) {
        consumers.emplace_back(consumer, i);
    }

    for (auto& p : producers) p.join();
    for (auto& c : consumers) c.join();

    printTopN();
    printTotalCars();
    printSeparator();
    std::cout << COLOR_CYAN << "SIMULATION COMPLETED SUCCESSFULLY" << COLOR_RESET << std::endl;

    return 0;
}