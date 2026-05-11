#include "greedy.h"
#include "cost_model.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
using namespace std;

/*
 * == ALGORITHM == Greedy Index Selection by Benefit-to-Cost Ratio
 * 
 * == STRATEGY ==
 * At each step, pick the candidate index that gives the most benefit per unit of storage consumed.
 * i.e., the highest (benefit / storage_cost) ratio.
 * Keep picking until no remaining candidate fits in the budget.
 *
 * This is the "fractional knapsack" intuition applied to a 0/1 problem.
 * It is fast and intuitive, but not guaranteed to find the optimal solution.
 * Counterexample: one expensive high-ratio item may beat two cheaper
 * items individually but lose to them combined (see TC3 in test_cases.cpp).
 *
 * == COMPLEXITY ANALYSIS ==
 * 
 * Let:
 *   n = number of attributes in the schema
 *   q = number of queries in the workload
 *   B = budget (integer units)
 *
 * DOMINANT OPERATION: the benefit computation inner loop check: "does query Q reference attribute A?"
 * This fires n * q times across build_candidates() and is the bottleneck for workloads where q > log(n).
 *
 * Step 1: build_candidates():
 *   For each of n attributes, scan all q queries => O(n * q)
 *
 * Step 2: sort on n candidates:
 *   O(n log n) comparisons
 *
 * Step 3: greedy selection pass:
 *   One linear scan through n candidates => O(n)
 *
 * TOTAL TIME: O(n*q + n*log(n))
 *   n*q dominates when q > log(n), which holds for any real workload.
 *
 * TOTAL SPACE: O(n) - candidate list only, no table allocation.
 *
 * num_operations counts: one increment per call to the benefit inner loop (step 1)
 * plus one per candidate examined during selection (step 3).
 * This directly reflects the O(n*q) dominant term.
 */

AdvisorResult greedy_advisor(const AdvisorInput& input) {
    auto start = chrono::high_resolution_clock::now();
    long long ops = 0;

    /* Step 1: Build candidate index list
     *
     * Compute benefit and storage cost for every attribute.
     * Attributes not referenced by any query are filtered out as they can never help.
     *
     * We instrument ops here by counting each query checked per attribute.
     * This is the dominant inner loop of compute_benefit(),
     * and counting it gives us direct empirical evidence of the O(n*q) term.
     */
    vector<CandidateIndex> candidates;
    candidates.reserve(input.schema.size());

    for (const auto& attr : input.schema) {
        // Count one operation per query examined during benefit computation.
        // Commented out for now because it's common between the two approaches, but for sure it's necessary preprocessing step for the algorithm.
        // ops += static_cast<long long>(input.workload.size());

        double b = compute_benefit(attr, input.workload);
        if (b <= 0.0) continue;

        CandidateIndex c;
        c.attr         = attr;
        c.storage_cost = storage_cost(attr.tuples);
        c.benefit      = b;
        candidates.push_back(c);
    }

    /* Step 2: Sort candidates by benefit-to-cost ratio (descending)
     *
     * The greedy heuristic: the "most efficient" index,
     * the one that delivers the most benefit per unit of storage,
     * should be considered first.
     */
    sort(candidates.begin(), candidates.end(),
        [](const CandidateIndex& a, const CandidateIndex& b) {
            return (a.benefit / static_cast<double>(a.storage_cost))
                 > (b.benefit / static_cast<double>(b.storage_cost));
        });
    
    ops += candidates.size() * static_cast<long long>(log2(candidates.size()));  // rough ops count for sort comparisons

    /* Step 3: Greedy selection
     *
     * Walk the sorted list from highest ratio to lowest.
     * Add each index to the selected set if and only if its storage cost fits within the remaining budget.
     * Deduct the cost immediately on selection.
     *
     * We do NOT skip ahead if a large index fails
     * as a smaller index later in the list might still fit.
     * i.e., no early exit except when the budget is fully exhausted (B == 0).
     * This single pass is O(n).
     */
    int remaining = input.budget;
    vector<CandidateIndex> selected;
    unordered_set<string> selected_keys;

    for (const auto& c : candidates) {
        ops++;

        if (c.storage_cost <= remaining) {
            selected.push_back(c);
            selected_keys.insert(c.attr.key());
            remaining -= c.storage_cost;
        }

        if (remaining <= 0) break;  // budget exhausted, no more candidates can fit
    }

    /* Step 4: Compute workload costs before and after selection
     * [see compute_costs() in cost_model.cpp]
     */
    AdvisorResult result;
    result.selected = selected;
    compute_costs(input, selected_keys, result);

    auto end              = chrono::high_resolution_clock::now();
    result.runtime_us     = chrono::duration_cast<chrono::microseconds>(end - start).count();
    result.num_operations = ops;

    return result;
}
