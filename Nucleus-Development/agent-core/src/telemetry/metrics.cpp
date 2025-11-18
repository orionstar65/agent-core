#include "agent/telemetry.hpp"
#include <iostream>
#include <map>
#include <vector>
#include <mutex>

namespace agent {

class MetricsImpl : public Metrics {
public:
    void increment(const std::string& name, int64_t value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name] += value;
    }
    
    void histogram(const std::string& name, double value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        histograms_[name].push_back(value);
    }
    
    void gauge(const std::string& name, double value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        gauges_[name] = value;
    }
    
    void dump() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "=== Metrics Snapshot ===\n";
        
        if (!counters_.empty()) {
            std::cout << "Counters:\n";
            for (const auto& [name, value] : counters_) {
                std::cout << "  " << name << ": " << value << "\n";
            }
        }
        
        if (!gauges_.empty()) {
            std::cout << "Gauges:\n";
            for (const auto& [name, value] : gauges_) {
                std::cout << "  " << name << ": " << value << "\n";
            }
        }
        
        if (!histograms_.empty()) {
            std::cout << "Histograms:\n";
            for (const auto& [name, values] : histograms_) {
                std::cout << "  " << name << ": " << values.size() << " samples\n";
            }
        }
    }

private:
    std::mutex mutex_;
    std::map<std::string, int64_t> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, std::vector<double>> histograms_;
};

std::unique_ptr<Metrics> create_metrics() {
    return std::make_unique<MetricsImpl>();
}

}
