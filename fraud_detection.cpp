// fraud_detection.cpp
// Core fraud detection algorithms using BFS and DFS
// Detects: connected components, suspicious clusters, fraud cycles
//
// Real-World Usage (JPMorgan / AML context):
// - BFS is used to trace the shortest money flow path between accounts
//   (money mule networks, layering stage in AML)
// - DFS is used to detect cycles in transaction graphs
//   (circular payment fraud, wash trading, round-tripping)
// - Connected Components reveal isolated fraud clusters
//   (structuring / smurfing detection)
//
// Scalability Notes:
// - unordered_map/unordered_set: O(1) average lookup, handles billions of nodes
// - BFS/DFS both O(V + E) — linear in graph size
// - For production at JPMorgan scale (~10B tx/day):
//   * Partition graph by region/currency using sharded hash maps
//   * Run BFS/DFS in parallel across partitions (OpenMP / TBB)
//   * Use approximate algorithms (MinHash, LSH) for near-duplicate detection
//   * Stream processing with Apache Kafka + Flink for real-time flagging

#include "fraud_detection.h"
#include <queue>
#include <stack>
#include <iostream>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// BFS: Find all accounts reachable from a source within max_depth hops
// Used to trace money flow paths (layering detection in AML)
// Time: O(V + E), Space: O(V)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> FraudDetector::bfsReachable(
        const Graph& graph,
        const std::string& source,
        int max_depth) {

    std::vector<std::string> visited_order;
    std::unordered_set<std::string> visited;
    // Queue holds {account_id, current_depth}
    std::queue<std::pair<std::string, int>> bfs_queue;

    bfs_queue.push({source, 0});
    visited.insert(source);

    while (!bfs_queue.empty()) {
        auto [current, depth] = bfs_queue.front();
        bfs_queue.pop();
        visited_order.push_back(current);

        if (depth >= max_depth) continue;

        // Traverse all outgoing transactions (neighbors)
        for (const auto& neighbor : graph.getNeighbors(current)) {
            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                bfs_queue.push({neighbor, depth + 1});
            }
        }
    }
    return visited_order;
}

// ─────────────────────────────────────────────────────────────────────────────
// DFS: Detect cycles in the transaction graph
// A cycle = circular money flow = potential round-trip fraud / wash trading
// Uses iterative DFS with recursion stack tracking
// Time: O(V + E), Space: O(V)
// ─────────────────────────────────────────────────────────────────────────────
bool FraudDetector::dfsDetectCycle(
        const Graph& graph,
        const std::string& start,
        std::vector<std::string>& cycle_path) {

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> rec_stack; // recursion stack
    std::unordered_map<std::string, std::string> parent; // for path reconstruction

    // Inner recursive lambda for DFS
    std::function<bool(const std::string&)> dfs = [&](const std::string& node) -> bool {
        visited.insert(node);
        rec_stack.insert(node);

        for (const auto& neighbor : graph.getNeighbors(node)) {
            if (visited.find(neighbor) == visited.end()) {
                parent[neighbor] = node;
                if (dfs(neighbor)) return true;
            } else if (rec_stack.find(neighbor) != rec_stack.end()) {
                // Found a back-edge: reconstruct the cycle path
                cycle_path.clear();
                std::string curr = node;
                while (curr != neighbor) {
                    cycle_path.push_back(curr);
                    curr = parent[curr];
                }
                cycle_path.push_back(neighbor);
                cycle_path.push_back(node); // close the cycle
                std::reverse(cycle_path.begin(), cycle_path.end());
                return true;
            }
        }
        rec_stack.erase(node);
        return false;
    };

    return dfs(start);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connected Components: Find all isolated account clusters
// Uses BFS from each unvisited node (undirected view of the graph)
// Small clusters with high internal tx volume = structuring / smurfing
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::vector<std::string>> FraudDetector::findConnectedComponents(
        const Graph& graph) {

    std::unordered_set<std::string> visited;
    std::vector<std::vector<std::string>> components;

    // Build an undirected adjacency view: combine forward + reverse edges
    std::unordered_map<std::string, std::unordered_set<std::string>> undirected;
    for (const auto& [src, txs] : graph.adjacency_list) {
        for (const auto& tx : txs) {
            undirected[src].insert(tx.to_account);
            undirected[tx.to_account].insert(src); // reverse edge
        }
    }

    // BFS from each unvisited account
    for (const auto& [account, _] : graph.nodes) {
        if (visited.find(account) != visited.end()) continue;

        // Start a new component
        std::vector<std::string> component;
        std::queue<std::string> q;
        q.push(account);
        visited.insert(account);

        while (!q.empty()) {
            std::string curr = q.front(); q.pop();
            component.push_back(curr);

            for (const auto& neighbor : undirected[curr]) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }
        components.push_back(component);
    }
    return components;
}

// ─────────────────────────────────────────────────────────────────────────────
// Flag suspicious accounts based on heuristic rules
// Rules derived from FATF guidelines and AML typologies:
//   1. Velocity: > threshold transactions in dataset
//   2. Fan-out: sends to many distinct accounts (structuring)
//   3. Amount anomaly: single tx > 3x average tx amount (large cash placement)
//   4. Round-trip: participates in detected cycles
// ─────────────────────────────────────────────────────────────────────────────
FraudReport FraudDetector::analyzeGraph(Graph& graph) {
    FraudReport report;

    // ── Step 1: Compute global statistics ──────────────────────────────────
    double total_amount = 0.0;
    int total_tx = 0;
    for (const auto& [src, txs] : graph.adjacency_list) {
        for (const auto& tx : txs) {
            total_amount += tx.amount;
            total_tx++;
        }
    }
    double avg_amount = (total_tx > 0) ? total_amount / total_tx : 0.0;
    report.total_transactions = total_tx;
    report.total_accounts = graph.getNodeCount();

    // ── Step 2: Flag high-velocity and high-fan-out accounts ──────────────
    int velocity_threshold = std::max(3, total_tx / (int)graph.nodes.size());
    for (auto& [id, node] : graph.nodes) {
        // Heuristic: if an account sent > 2x average transactions, flag it
        auto& outgoing = graph.adjacency_list[id];
        std::unordered_set<std::string> unique_recipients;
        double max_single_tx = 0.0;

        for (const auto& tx : outgoing) {
            unique_recipients.insert(tx.to_account);
            max_single_tx = std::max(max_single_tx, tx.amount);
        }

        bool high_velocity   = (int)outgoing.size() > velocity_threshold;
        bool high_fan_out    = (int)unique_recipients.size() > 3;
        bool amount_anomaly  = max_single_tx > (3.0 * avg_amount);

        if (high_velocity || high_fan_out || amount_anomaly) {
            node.is_flagged = true;
            report.suspicious_accounts.push_back(id);
        }
    }

    // ── Step 3: Detect fraud cycles (DFS) ────────────────────────────────
    std::unordered_set<std::string> cycle_nodes;
    for (const auto& [id, _] : graph.nodes) {
        std::vector<std::string> cycle_path;
        if (dfsDetectCycle(graph, id, cycle_path) && !cycle_path.empty()) {
            // Deduplicate: only add new cycles
            std::string cycle_key;
            auto sorted_path = cycle_path;
            std::sort(sorted_path.begin(), sorted_path.end());
            for (auto& s : sorted_path) cycle_key += s + "|";

            if (report.seen_cycle_keys.find(cycle_key) == report.seen_cycle_keys.end()) {
                report.seen_cycle_keys.insert(cycle_key);
                report.fraud_cycles.push_back(cycle_path);
                for (auto& node_id : cycle_path) {
                    graph.nodes[node_id].is_flagged = true;
                    cycle_nodes.insert(node_id);
                }
            }
        }
    }

    // ── Step 4: Find connected components & suspicious clusters ──────────
    auto components = findConnectedComponents(graph);
    for (auto& component : components) {
        // A suspicious cluster: small component (2–10 accounts) with many
        // internal transactions — typical of smurfing / layering networks
        if (component.size() >= 2 && component.size() <= 10) {
            int internal_tx = 0;
            for (const auto& acc : component) {
                for (const auto& tx : graph.adjacency_list[acc]) {
                    // Check if destination is also in this component
                    for (const auto& other : component) {
                        if (tx.to_account == other) internal_tx++;
                    }
                }
            }
            // Flag cluster if it has high internal transaction density
            double density = (double)internal_tx / (component.size() * (component.size() - 1));
            if (density > 0.3) {
                report.suspicious_clusters.push_back(component);
                for (auto& acc : component) {
                    graph.nodes[acc].is_flagged = true;
                    if (std::find(report.suspicious_accounts.begin(),
                                  report.suspicious_accounts.end(), acc)
                        == report.suspicious_accounts.end()) {
                        report.suspicious_accounts.push_back(acc);
                    }
                }
            }
        }
        report.components.push_back(component);
    }

    report.flagged_count = (int)report.suspicious_accounts.size();
    return report;
}

// ─────────────────────────────────────────────────────────────────────────────
// Print the fraud report to stdout
// ─────────────────────────────────────────────────────────────────────────────
void FraudDetector::printReport(const FraudReport& report) {

    std::cout << "       FRAUD DETECTION REPORT           \n";
    

    std::cout << "Total Transactions Analyzed : " << report.total_transactions << "\n";
    std::cout << "Total Accounts              : " << report.total_accounts << "\n";
    std::cout << "Suspicious Accounts Flagged : " << report.flagged_count << "\n";
    std::cout << " Fraud Cycles Detected       : " << report.fraud_cycles.size() << "\n";
    std::cout << "  Suspicious Clusters         : " << report.suspicious_clusters.size() << "\n";
    std::cout << " Connected Components        : " << report.components.size() << "\n\n";

    if (!report.fraud_cycles.empty()) {
        std::cout << "FRAUD CYCLES \n";
        for (int i = 0; i < (int)report.fraud_cycles.size(); i++) {
            std::cout << "  Cycle " << (i + 1) << ": ";
            for (const auto& acc : report.fraud_cycles[i]) {
                std::cout << acc << " - ";
            }
            std::cout << "(back)\n";
        }
    }

    if (!report.suspicious_accounts.empty()) {
        std::cout << "\nSUSPICIOUS ACCOUNTS : \n";
        for (const auto& acc : report.suspicious_accounts) {
            std::cout << "  warning  " << acc << "\n";
        }
    }

    if (!report.suspicious_clusters.empty()) {
        std::cout << "\n── SUSPICIOUS CLUSTERS ───────────────────\n";
        for (int i = 0; i < (int)report.suspicious_clusters.size(); i++) {
            std::cout << "  Cluster " << (i + 1) << ": { ";
            for (const auto& acc : report.suspicious_clusters[i]) {
                std::cout << acc << " ";
            }
            std::cout << "}\n";
        }
    }
    std::cout << "\n";
}
