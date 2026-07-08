
// Build: g++ -std=c++17 -O2 -o fraud_detector main.cpp Graph.cpp fraud_detection.cpp
// Run:   ./fraud_detector sample_transactions.csv

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include "Graph.h"
#include "fraud_detection.h"


std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        // Strip whitespace
        while (!field.empty() && (field.front() == ' ' || field.front() == '"'))
            field.erase(field.begin());
        while (!field.empty() && (field.back() == ' ' || field.back() == '"'))
            field.pop_back();
        fields.push_back(field);
    }
    return fields;
}


int loadCSV(const std::string& filename, Graph& graph) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << "\n";
        return -1;
    }

    std::string line;
    int line_num = 0;
    int loaded   = 0;
    int skipped  = 0;

    while (std::getline(file, line)) {
        line_num++;

        // Skip header row
        if (line_num == 1) continue;
        // Skip empty lines
        if (line.empty()) continue;

        auto fields = parseCSVLine(line);
        if (fields.size() < 5) {
            std::cerr << "WARNING: Skipping malformed line " << line_num << "\n";
            skipped++;
            continue;
        }

        std::string tx_id       = fields[0];
        std::string from_acc    = fields[1];
        std::string to_acc      = fields[2];
        double amount           = 0.0;
        std::string timestamp   = fields[4];

        try {
            amount = std::stod(fields[3]);
        } catch (...) {
            std::cerr << "WARNING: Invalid amount on line " << line_num << "\n";
            skipped++;
            continue;
        }

        if (amount <= 0) {
            skipped++;
            continue;
        }

        graph.addTransaction(from_acc, to_acc, amount, timestamp, tx_id);
        loaded++;
    }

    std::cout << "[OK] Loaded " << loaded << " transactions";
    if (skipped > 0) std::cout << " (" << skipped << " skipped)";
    std::cout << " from " << filename << "\n";
    return loaded;
}

// Write fraud results to CSV for the Python/Streamlit GUI
void exportResultsCSV(const Graph& graph, const FraudReport& report,
                      const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out.is_open()) {
        std::cerr << "ERROR: Cannot write output file: " << output_file << "\n";
        return;
    }

    // Header
    out << "account_id,total_sent,total_received,tx_count,is_flagged,flag_reason\n";

    for (const auto& [id, node] : graph.nodes) {
        // Determine flag reason
        std::string reason = "";
        if (node.is_flagged) {
            // Check if in a cycle
            for (const auto& cycle : report.fraud_cycles) {
                for (const auto& c : cycle) {
                    if (c == id) { reason = "fraud_cycle"; break; }
                }
                if (!reason.empty()) break;
            }
            // Check if in suspicious cluster
            if (reason.empty()) {
                for (const auto& cluster : report.suspicious_clusters) {
                    for (const auto& c : cluster) {
                        if (c == id) { reason = "suspicious_cluster"; break; }
                    }
                    if (!reason.empty()) break;
                }
            }
            if (reason.empty()) reason = "high_velocity_or_amount";
        }

        out << id << ","
            << std::fixed << std::setprecision(2) << node.total_sent << ","
            << node.total_received << ","
            << node.transaction_count << ","
            << (node.is_flagged ? "1" : "0") << ","
            << reason << "\n";
    }

    std::cout << "Account results exported to: " << output_file << "\n";
    out.close();
}

// Export transaction edges for graph visualization
void exportEdgesCSV(const Graph& graph, const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out.is_open()) return;

    out << "tx_id,from_account,to_account,amount,timestamp,src_flagged,dst_flagged\n";
    for (const auto& [src, txs] : graph.adjacency_list) {
        for (const auto& tx : txs) {
            bool src_flagged = graph.nodes.count(src) && graph.nodes.at(src).is_flagged;
            bool dst_flagged = graph.nodes.count(tx.to_account)
                               && graph.nodes.at(tx.to_account).is_flagged;
            out << tx.tx_id << ","
                << src << ","
                << tx.to_account << ","
                << std::fixed << std::setprecision(2) << tx.amount << ","
                << tx.timestamp << ","
                << (src_flagged ? "1" : "0") << ","
                << (dst_flagged ? "1" : "0") << "\n";
        }
    }
    std::cout << "Edge data exported to: " << output_file << "\n";
    out.close();
}

int main(int argc, char* argv[]) {
    std::cout << "\nBANKING FRAUD DETECTION SYSTEM v1.0\n";
    std::cout << "Powered by BFS/DFS Graph Algorithms\n";
    std::cout << "====================================\n\n";

    // Default input file
    std::string input_file = (argc > 1) ? argv[1] : "sample_transactions.csv";

    // ── Build Transaction Graph ───────────────────────────────────────────
    Graph graph;
    auto t_start = std::chrono::high_resolution_clock::now();

    int loaded = loadCSV(input_file, graph);
    if (loaded <= 0) {
        std::cerr << "No transactions loaded. Exiting.\n";
        return 1;
    }

    auto t_load = std::chrono::high_resolution_clock::now();
    double load_ms = std::chrono::duration<double, std::milli>(t_load - t_start).count();
    std::cout << "[TIME] Graph build time: " << load_ms << " ms\n";

    graph.printSummary();

    // ── Run Fraud Detection ───────────────────────────────────────────────
    std::cout << "\nRunning fraud detection algorithms...\n";
    auto t_analysis_start = std::chrono::high_resolution_clock::now();

    FraudReport report = FraudDetector::analyzeGraph(graph);

    auto t_analysis_end = std::chrono::high_resolution_clock::now();
    double analysis_ms = std::chrono::duration<double, std::milli>(
        t_analysis_end - t_analysis_start).count();
    std::cout << "[TIME] Analysis time: " << analysis_ms << " ms\n";

    // ── Print Report ──────────────────────────────────────────────────────
    FraudDetector::printReport(report);

    // ── Export Results for GUI ────────────────────────────────────────────
    exportResultsCSV(graph, report, "fraud_accounts.csv");
    exportEdgesCSV(graph, "fraud_edges.csv");

    // ── Demonstrate BFS Path Tracing ──────────────────────────────────────
    if (!report.suspicious_accounts.empty()) {
        std::string seed = report.suspicious_accounts[0];
        std::cout << "\nBFS Money Flow from: " << seed << " (depth=3)\n";
        auto reachable = FraudDetector::bfsReachable(graph, seed, 3);
        std::cout << "   Reachable accounts: ";
        for (const auto& acc : reachable) std::cout << acc << " ";
        std::cout << "\n";
    }

    std::cout << "\nAnalysis complete.\n\n";
    return 0;
}
