#include "dp.h"
#include "cost_model.h"

#include <chrono>
#include <unordered_set>
#include <unordered_map>
using namespace std;

/*
 * == ALGORITHM == Optimal Index Selection via 0/1 Knapsack Dynamic Programming
 *
 * == STRATEGY ==
 * Model the problem as a 0/1 knapsack:
 *   - Each candidate index is an "item" with a weight (storage_cost) and a value (benefit).
 *   - The knapsack capacity is the storage budget B.
 *   - We want to maximize total value without exceeding capacity.
 *
 * We fill a 2D table bottom-up where:
 *   dp[i][b] = maximum total benefit achievable using the first i
 *              candidates with a budget of b units.
 *
 * After filling, we traceback through the table to recover which candidates were actually selected.
 *
 * This approach is GUARANTEED to find the optimal solution,
 * unlike Greedy, it considers all combinations implicitly.
 *
 * == COMPLEXITY ANALYSIS ==
 *
 * Let:
 *   n = number of attributes in the schema
 *   q = number of queries in the workload
 *   B = budget (integer units, discretized)
 *
 * DOMINANT OPERATION: the DP cell fill: the comparison and conditional assignment inside the double loop.
 * This fires exactly n * (B+1) times and is the bottleneck when B is large.
 *
 * Step 1: build_candidates():
 *   For each of n attributes, scan all q queries => O(n * q)
 *
 * Step 2: Allocate DP table:
 *   (n+1) * (B+1) 2d array of double => O(n * B) space and time to zero-initialize.
 *
 * Step 3: Fill DP table:
 *   Two nested loops: outer over n candidates, inner over B+1 budget levels.
 *   Each cell is filled in O(1) => O(n * B) total.
 *
 * Step 4: Traceback:
 *   One pass backwards through n rows => O(n)
 *
 * TOTAL TIME:  O(n*q + n*B)
 *   Which term dominates depends on the relationship between q and B.
 *   For small B (tight budget, few units), n*q dominates.
 *   For large B (generous budget, many units), n*B dominates and
 *   DP becomes significantly slower than Greedy.
 *
 * TOTAL SPACE: O(n * B) - the DP table is the dominant allocation.
 *   Greedy uses only O(n); this is the key memory trade-off.
 *
 * num_operations counts: one per query checked during build (step 1)
 * plus one per cell filled during the DP loops (step 3).
 * Together these capture both O(n*q) and O(n*B) terms empirically.
 */

AdvisorResult dp_advisor(const AdvisorInput& input) {
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

    int n = static_cast<int>(candidates.size());
    int B = input.budget;

    /* Step 2: Allocate the DP table
     *
     * dp[i][b] = maximum total benefit achievable by considering only the
     *            first i candidates and using at most b units of storage.
     *
     * Base case: dp[0][b] = 0 for all b : with zero candidates, benefit is 0.
     * The vector constructor zero-initializes all entries, so base cases are already set.
     */
    vector<vector<double>> dp(n + 1, vector<double>(B + 1, 0.0));

    /* Step 3: Fill the DP table (0/1 knapsack recurrence)
     *
     * For each candidate i and each budget level b, we decide:
     *   Option A: skip candidate i:
     *     Best we can do is whatever we achieved without it = dp[i-1][b]
     *   Option B: include candidate i (only valid if it fits):
     *     Benefit of i added to the best result for the remaining budget = dp[i-1][b - cost] + benefit
     *
     * We take whichever option gives higher total benefit.
     *
     * Key insight: by iterating over ALL budget levels b for each candidate,
     * we implicitly evaluate every possible subset. No subset is missed.
     */
    for (int i = 1; i <= n; i++) {
        int    cost    = candidates[i - 1].storage_cost;
        double benefit = candidates[i - 1].benefit;

        for (int b = 0; b <= B; b++) {
            ops++;

            // Option A: do not build index i, carry forward without it.
            dp[i][b] = dp[i - 1][b];

            // Option B: build index i, only valid if it fits in budget b.
            if (cost <= b) {
                double with_index = dp[i - 1][b - cost] + benefit;

                // Take the better of the two options.
                if (with_index > dp[i][b]) {
                    dp[i][b] = with_index;
                }
            }
            // Now dp[i][b] holds the optimal benefit for the first i candidates within budget b.
        }
    }

    /* Step 4: Traceback
     *
     * The DP table tells us the OPTIMAL VALUE but not which items were chosen.
     * We recover the selection by walking backwards through the table.
     *
     * At each row i, if dp[i][b] != dp[i-1][b], candidate i must have been included,
     * because the only way the value changed is if we took it.
     * When we take it, subtract its cost from the remaining budget.
     *
     * Start from dp[n][B] (the global optimum) and work back to row 0.
     */
    vector<CandidateIndex> selected;
    unordered_set<string> selected_keys;

    int b = B;
    for (int i = n; i >= 1; i--) {
        ops++;

        // If the value changed from row i-1 to row i, candidate i was taken.
        if (dp[i][b] != dp[i - 1][b]) {
            selected.push_back(candidates[i - 1]);
            selected_keys.insert(candidates[i - 1].attr.key());
            // Recover the budget that was available before taking this item.
            b -= candidates[i - 1].storage_cost;
        }
        // If dp[i][b] == dp[i-1][b], candidate i was skipped, do nothing.
    }

    /* Step 5: Compute workload costs before and after selection
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
