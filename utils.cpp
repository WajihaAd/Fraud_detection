#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>

#include "Graph.h"
#include "fraud_detection.h"

using namespace std;

// ─────────────────────────────
// CSV PARSER
// ─────────────────────────────
vector<string> parseCSVLine(const string& line) {

    vector<string> fields;
    stringstream ss(line);
    string field;

    while (getline(ss, field, ',')) {

        while (!field.empty() &&
              (field.front() == ' ' || field.front() == '"')) {
            field.erase(field.begin());
        }

        while (!field.empty() &&
              (field.back() == ' ' || field.back() == '"')) {
            field.pop_back();
        }

        fields.push_back(field);
    }

    return fields;
}

// ─────────────────────────────
// LOAD CSV INTO GRAPH
// ─────────────────────────────
int loadCSV(const string& filename, Graph& graph) {

    ifstream file(filename);

    if (!file.is_open()) {
        cout << "ERROR: Cannot open file\n";
        return -1;
    }

    string line;
    int line_num = 0;
    int loaded = 0;
    int skipped = 0;

    while (getline(file, line)) {

        line_num++;

        if (line_num == 1 || line.empty())
            continue;

        vector<string> fields = parseCSVLine(line);

        if (fields.size() < 5) {
            skipped++;
            continue;
        }

        string tx_id = fields[0];
        string from  = fields[1];
        string to    = fields[2];
        string time  = fields[4];

        double amount = 0;

        try {
            amount = stod(fields[3]);
        } catch (...) {
            skipped++;
            continue;
        }

        if (amount <= 0) {
            skipped++;
            continue;
        }

        graph.addTransaction(from, to, amount, time, tx_id);
        loaded++;
    }

    cout << "Loaded " << loaded << " transactions\n";

    return loaded;
}

// ─────────────────────────────
// EXPORT ACCOUNTS CSV
// ─────────────────────────────
void exportResultsCSV(const Graph& graph,
                      const FraudReport& report,
                      const string& file) {

    ofstream out(file);

    if (!out.is_open()) return;

    out << "account_id,total_sent,total_received,tx_count,is_flagged,reason\n";

    for (const auto& pair : graph.nodes) {

        const string& id = pair.first;
        const AccountNode& node = pair.second;

        string reason = "";

        if (node.is_flagged) {
            reason = "suspicious";
        }

        out << id << ","
            << fixed << setprecision(2)
            << node.total_sent << ","
            << node.total_received << ","
            << node.transaction_count << ","
            << node.is_flagged << ","
            << reason << "\n";
    }

    out.close();

    cout << "Exported accounts CSV\n";
}

// ─────────────────────────────
// EXPORT EDGES CSV
// ─────────────────────────────
void exportEdgesCSV(const Graph& graph,
                    const string& file) {

    ofstream out(file);

    if (!out.is_open()) return;

    out << "tx_id,from,to,amount,time\n";

    for (const auto& pair : graph.adjacency_list) {

        const string& src = pair.first;
        const vector<Transaction>& txs = pair.second;

        for (const auto& tx : txs) {

            out << tx.tx_id << ","
                << src << ","
                << tx.to_account << ","
                << tx.amount << ","
                << tx.timestamp << "\n";
        }
    }

    out.close();

    cout << "Exported edges CSV\n";
}