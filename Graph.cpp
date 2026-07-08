// Graph.cpp - Implementation of the adjacency list graph
// Uses unordered_map for O(1) average-case insertions and lookups
// Scalable to billions of transactions via hash-based storage

#include "Graph.h"
#include <iostream>
#include <iomanip>

// Add a directed transaction edge from src to dst
// Time complexity: O(1) amortized with unordered_map
void Graph::addTransaction(const std::string& src,
                           const std::string& dst,
                           double amount,
                           const std::string& timestamp,
                           const std::string& tx_id) {
    // Create Transaction edge object
    Transaction tx;
    tx.to_account = dst;
    tx.amount     = amount;
    tx.timestamp  = timestamp;
    tx.tx_id      = tx_id;

    // Insert into adjacency list (creates empty vector if key doesn't exist)
    adjacency_list[src].push_back(tx);

    // Initialize source node if it doesn't exist
    if (nodes.find(src) == nodes.end()) {
        nodes[src] = {src, 0.0, 0.0, 0, false};
    }
    // Initialize destination node if it doesn't exist
    if (nodes.find(dst) == nodes.end()) {
        nodes[dst] = {dst, 0.0, 0.0, 0, false};
    }

    // Update node statistics
    nodes[src].total_sent      += amount;
    nodes[src].transaction_count++;
    nodes[dst].total_received  += amount;
    nodes[dst].transaction_count++;

    // Ensure dst appears in adjacency list (even with no outgoing edges)
    if (adjacency_list.find(dst) == adjacency_list.end()) {
        adjacency_list[dst] = {};
    }
}

// Return list of account IDs that received money from account_id
std::vector<std::string> Graph::getNeighbors(const std::string& account_id) const {
    std::vector<std::string> neighbors;
    auto it = adjacency_list.find(account_id);
    if (it != adjacency_list.end()) {
        for (const auto& tx : it->second) {
            neighbors.push_back(tx.to_account);
        }
    }
    return neighbors;
}

// Count unique accounts
int Graph::getNodeCount() const {
    return static_cast<int>(nodes.size());
}

// Count total transactions (edges)
int Graph::getEdgeCount() const {
    int count = 0;
    for (const auto& [account, txs] : adjacency_list) {
        count += static_cast<int>(txs.size());
    }
    return count;
}

// Check if account exists in graph
bool Graph::accountExists(const std::string& account_id) const {
    return nodes.find(account_id) != nodes.end();
}

// Print a readable summary of the graph
void Graph::printSummary() const {
    std::cout << "\n========== GRAPH SUMMARY ==========\n";
    std::cout << "Total Accounts (Nodes): " << getNodeCount() << "\n";
    std::cout << "Total Transactions (Edges): " << getEdgeCount() << "\n";
    std::cout << "====================================\n";

    // Print top accounts by transaction count
    std::cout << "\nTop Accounts:\n";
    std::cout << std::left
              << std::setw(15) << "Account"
              << std::setw(12) << "Sent ($)"
              << std::setw(12) << "Received ($)"
              << std::setw(10) << "Tx Count"
              << "Flagged\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& [id, node] : nodes) {
        std::cout << std::left
                  << std::setw(15) << id
                  << std::setw(12) << std::fixed << std::setprecision(2) << node.total_sent
                  << std::setw(12) << node.total_received
                  << std::setw(10) << node.transaction_count
                  << (node.is_flagged ? "YES" : "NO") << "\n";
    }
}
