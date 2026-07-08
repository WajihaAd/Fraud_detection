#pragma once
// fraud_detection.h - Header for BFS/DFS fraud detection algorithms

#include "Graph.h"
#include <vector>
#include <string>
#include <unordered_set>

// Aggregated fraud analysis results
struct FraudReport {
    int total_transactions = 0;
    int total_accounts     = 0;
    int flagged_count      = 0;

    std::vector<std::string>              suspicious_accounts;
    std::vector<std::vector<std::string>> fraud_cycles;
    std::vector<std::vector<std::string>> suspicious_clusters;
    std::vector<std::vector<std::string>> components;

    // Internal dedup set for cycle detection
    std::unordered_set<std::string> seen_cycle_keys;
};

class FraudDetector {
public:
    // BFS: return all accounts reachable from source within max_depth hops
    static std::vector<std::string> bfsReachable(
        const Graph& graph,
        const std::string& source,
        int max_depth = 4);

    // DFS: detect cycles starting from start node; fills cycle_path on hit
    static bool dfsDetectCycle(
        const Graph& graph,
        const std::string& start,
        std::vector<std::string>& cycle_path);

    // Find all connected components in undirected view of graph
    static std::vector<std::vector<std::string>> findConnectedComponents(
        const Graph& graph);

    // Master analysis: runs all detection algorithms, returns full report
    static FraudReport analyzeGraph(Graph& graph);

    // Print formatted report to stdout
    static void printReport(const FraudReport& report);
};
