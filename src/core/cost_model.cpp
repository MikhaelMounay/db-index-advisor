#include "cost_model.h"

#include <cmath>
#include <algorithm>
#include <unordered_map>
using namespace std;

/*
 * full_scan_cost - linear scan.
 * Reading every row in a table is proportional to the number of rows.
 */
double full_scan_cost(int tuples) {
    return static_cast<double>(tuples);
}

/*
 * index_scan_cost - B-tree traversal.
 * A B-tree of n rows has height approx. log2(n), so lookup touches log2(n) nodes.
 */
double index_scan_cost(int tuples) {
    return log2(static_cast<double>(tuples));
}

/*
 * storage_cost - how many integer budget units one index consumes.
 * We discretize to 1 unit per 1000 rows, with a minimum of 1,
 * so that indexes on tiny tables still consume some budget.
 * [see cost_model.h for discussion of this function's rationale.]
 */
int storage_cost(int tuples) {
    return max(1, tuples / 1000);
}

/*
 * compute_benefit - total weighted cost reduction from indexing one attribute.
 *
 * Only queries that actually reference this attribute benefit from the index.
 * The gain per query is: frequency * (full_scan - index_scan).
 * We sum that gain over all qualifying queries.
 *
 * The unordered_set lookup q.attributes.count(key) is O(1),
 * keeping the total complexity of this function at O(q) where q = number of queries.
 */
double compute_benefit(const Attribute& attr, const vector<Query>& workload) {
    double total = 0.0;
    const string key = attr.key();

    // Per-query gain is constant for a given attribute (same table size).
    double gain_per_access = full_scan_cost(attr.tuples) - index_scan_cost(attr.tuples);

    for (const auto& query : workload) {
        if (query.attributes.count(key)) {
            // This query references our attribute, it benefits from the index.
            total += static_cast<double>(query.frequency) * gain_per_access;
        }
    }

    return total;
}

/*
 * build_candidates - build and filter the candidate index list.
 *
 * For every attribute in the schema, compute its benefit and storage cost.
 * Skip attributes with zero (or negative) benefit,
 * they are not referenced by any query and cannot improve execution cost.
 *
 * Complexity: O(n * q) where n = schema size, q = workload size.
 * (compute_benefit is O(q) and is called once per attribute.)
 */
vector<CandidateIndex> build_candidates(const AdvisorInput& input) {
    vector<CandidateIndex> candidates;
    candidates.reserve(input.schema.size());

    for (const auto& attr : input.schema) {
        double b = compute_benefit(attr, input.workload);
        if (b <= 0.0) continue;  // unreferenced attribute => skip

        CandidateIndex c;
        c.attr         = attr;
        c.storage_cost = storage_cost(attr.tuples);
        c.benefit      = b;
        candidates.push_back(c);
    }

    return candidates;
}

/*
 * compute_costs - calculate before/after workload cost given a selection.
 *
 * For each query, for each attribute it references,
 * we check whether that attribute was indexed.
 * If so, we use index_scan_cost;
 * otherwise full_scan_cost.
 * Results are written into the AdvisorResult.
 */
void compute_costs(const AdvisorInput& input,
                   const unordered_set<string>& selected_keys,
                   AdvisorResult& result)
{
    // Build a fast lookup map from attribute key -> tuple count.
    // We need tuple counts to call the cost functions, and scanning the
    // full schema for every (query, attribute) pair would be O(n*q*a),
    // this map reduces that inner lookup to O(1).
    unordered_map<string, int> tuples_for;
    for (const auto& attr : input.schema) {
        tuples_for[attr.key()] = attr.tuples;
    }

    double cost_before = 0.0;
    double cost_after  = 0.0;

    for (const auto& query : input.workload) {
        for (const auto& attr_key : query.attributes) {
            auto it = tuples_for.find(attr_key);
            if (it == tuples_for.end()) continue;  // attribute not in schema => skip

            int tuples = it->second;
            double fc  = full_scan_cost(tuples);

            cost_before += static_cast<double>(query.frequency) * fc;

            if (selected_keys.count(attr_key)) {
                // Index exists => use the cheaper index scan cost.
                cost_after += static_cast<double>(query.frequency) * index_scan_cost(tuples);
            } else {
                // No index => fall back to full scan.
                cost_after += static_cast<double>(query.frequency) * fc;
            }
        }
    }

    result.cost_before = cost_before;
    result.cost_after  = cost_after;
    result.cost_saved  = cost_before - cost_after;
}
