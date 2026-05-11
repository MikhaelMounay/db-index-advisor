#pragma once

#include "types.h"
using namespace std;

/*
 * cost_model.h - Query execution cost estimation functions.
 *
 * Both algorithms use these functions identically, which guarantees
 * that their benefit computations are always on the same footing.
 *
 * Cost model:
 *   full_scan_cost(n)  = n          (linear scan through all rows)
 *   index_scan_cost(n) = log2(n)    (B-tree traversal, logarithmic depth)
 *
 * Storage cost of one index:
 *   storage_cost(n) = max(1, n / 1000)   (integer units, minimum 1)
 * 
 *   This is just a simple function to give some weight to larger tables,
 *   but in reality the storage cost of an index would depend on
 *   the number of distinct values, the size of the indexed columns, and other factors.
 *   However, for the sake of this project, we use a simple function that grows with the number of tuples.
 * 
 * Benefit of indexing one attribute across the workload:
 *   For each query that references this attribute:
 *     contribution = frequency * (full_scan_cost - index_scan_cost)
 *   Summed over all such queries.
 */

double full_scan_cost(int tuples);
double index_scan_cost(int tuples);
int    storage_cost(int tuples);
double compute_benefit(const Attribute& attr, const vector<Query>& workload);

/*
 * build_candidates - shared helper used by both algorithms.
 *
 * Constructs the list of CandidateIndex values from the schema and workload.
 * Attributes that are not referenced by any query (benefit = 0) are filtered out.
 * They can never help, so there is no reason to consider them during selection.
 *
 * Both algorithms call this function before their own logic begins,
 * ensuring they operate on the exact same candidate set.
 */
vector<CandidateIndex> build_candidates(const AdvisorInput& input);

/*
 * compute_costs - shared helper used by both algorithms.
 *
 * Given an AdvisorInput and a set of selected index keys,
 * computes cost_before, cost_after, and cost_saved and writes them into result.
 */
void compute_costs(const AdvisorInput& input,
                   const unordered_set<string>& selected_keys,
                   AdvisorResult& result);
