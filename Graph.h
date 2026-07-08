#pragma once
// Graph.h - Adjacency list graph representation for fraud detection
// Supports directed weighted graphs for banking transaction networks

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>

// Represents a single transaction edge in the graph
struct Transaction {
    std::string to_account;     // Destination account ID
    double amount;              // Transaction amount in USD
    std::string timestamp;      // Transaction timestamp
    std::string tx_id;          // Unique transaction identifier
};

// Represents an account node with metadata
struct AccountNode {
    std::string account_id;
    double total_sent;          // Sum of all outgoing transactions
    double total_received;      // Sum of all incoming transactions
    int transaction_count;      // Total number of transactions
    bool is_flagged;            // Flagged as suspicious
};

// Graph class using adjacency list with unordered_map for O(1) lookups
class Graph {
public:
    // adjacency_list[account] -> list of outgoing transactions
    std::unordered_map<std::string, std::vector<Transaction>> adjacency_list;

    // Node metadata indexed by account ID
    std::unordered_map<std::string, AccountNode> nodes;

    // Add a directed edge (transaction) from src to dst
    void addTransaction(const std::string& src,
                        const std::string& dst,
                        double amount,
                        const std::string& timestamp,
                        const std::string& tx_id);

    // Get all neighbors (accounts that received money from account_id)
    std::vector<std::string> getNeighbors(const std::string& account_id) const;

    // Get total number of unique accounts (nodes)
    int getNodeCount() const;

    // Get total number of transactions (edges)
    int getEdgeCount() const;

    // Print graph summary to stdout
    void printSummary() const;

    // Check if an account exists in the graph
    bool accountExists(const std::string& account_id) const;
};
