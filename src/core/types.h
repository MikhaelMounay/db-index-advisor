#pragma once

#include <string>
#include <vector>
#include <unordered_set>
using namespace std;

/*
 * types.h - Shared data structures for the Index Selection Advisor.
 */


/* Core domain types */

/*
 * Attribute - one indexable column in the schema.
 *
 * For simplicity, 'tuples' is copied from the parent relation so that cost functions
 * never need to join back to a separate relation list during computation.
 *
 * key() returns the canonical "Relation.Attribute" string used as a
 * lookup token in Query::attributes sets.
 */
struct Attribute {
    string      relation;   // e.g. "Orders"
    string      name;       // e.g. "customer_id"
    int         tuples;     // estimated number of rows in the relation

    string key() const {
        return relation + "." + name;
    }
};

/*
 * Query - a single query in the workload.
 *
 * 'attributes' is an unordered_set<string> of "Relation.Attribute" keys
 * so that membership checks inside the benefit computation loop are O(1)
 * rather than O(|attributes|) as they would be with a vector.
 *
 * 'frequency' is a weight: how many times this query runs per unit time.
 * Higher frequency => higher benefit from indexing its attributes.
 */
struct Query {
    string                  label;       // human-readable SQL description
    unordered_set<string>   attributes;  // attribute keys referenced in WHERE / JOIN
    int                     frequency;   // call weight
};

/*
 * CandidateIndex - an attribute that could be indexed,
 * augmented with pre-computed cost and benefit values.
 *
 * Both algorithms build the same candidate list before running their selection logic,
 * so the results are always directly comparable.
 */
struct CandidateIndex {
    Attribute attr;
    int       storage_cost;  // integer units: max(1, tuples / 1000) [see cost_model.h]
    double    benefit;       // total weighted cost reduction across the workload
};

/*
 * AdvisorInput - the full problem instance passed to both algorithms.
 */
struct AdvisorInput {
    vector<Attribute>   schema;    // all indexable attributes
    vector<Query>       workload;  // all queries with frequency weights
    int                 budget;   // total storage budget in integer units
};

/*
 * AdvisorResult - what each algorithm returns.
 *
 * 'num_operations' counts the dominant atomic operation defined in each algorithm's complexity analysis comment.
 * It lets test cases compare empirical operation counts against theoretical complexity directly.
 */
struct AdvisorResult {
    vector<CandidateIndex>  selected;         // indexes chosen by the algorithm
    double                  cost_before;      // total workload cost with no indexes
    double                  cost_after;       // total workload cost with chosen indexes
    double                  cost_saved;       // cost_before - cost_after
    long long               runtime_us;       // wall-clock time in microseconds
    long long               num_operations;   // dominant operation counter
};
