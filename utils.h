#pragma once

#include <string>
#include <vector>
#include "Graph.h"
#include "fraud_detection.h"

std::vector<std::string> parseCSVLine(const std::string& line);

int loadCSV(const std::string& filename, Graph& graph);

void exportResultsCSV(const Graph& graph,
                      const FraudReport& report,
                      const std::string& file);

void exportEdgesCSV(const Graph& graph,
                    const std::string& file);