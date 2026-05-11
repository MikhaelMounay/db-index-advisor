#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <cmath>

#include "../core/types.h"
#include "../core/greedy.h"
#include "../core/dp.h"

using namespace std;

/*
 * test_cases.cpp - Test runner for the Index Selection Advisor.
 */

 /* Output helpers */

static void print_separator(int width = 72) {
    cout << string(width, '=') << "\n";
}

static void print_result(const string& algo_name, const AdvisorResult& r) {
    cout << "  [" << algo_name << "]\n";

    // Selected indexes
    cout << "    Selected : ";
    if (r.selected.empty()) {
        cout << "(none)\n";
    } else {
        for (size_t i = 0; i < r.selected.size(); i++) {
            if (i > 0) cout << ", ";
            cout << r.selected[i].attr.key()
                      << " (cost=" << r.selected[i].storage_cost
                      << ", benefit=" << fixed << setprecision(1)
                      << r.selected[i].benefit << ")";
        }
        cout << "\n";
    }

    cout << fixed << setprecision(2);
    cout << "    Cost     : before=" << r.cost_before
              << "  after=" << r.cost_after
              << "  saved=" << r.cost_saved << "\n";
    cout << "    Ops      : " << r.num_operations << "\n";
    cout << "    Runtime  : " << r.runtime_us << " μs\n";
}

static void run_test(const string& name,
                     const string& purpose,
                     const AdvisorInput& input)
{
    print_separator();
    cout << "\n";
    cout << "TEST: " << name << "\n";
    cout << "PURPOSE: " << purpose << "\n";
    cout << "INPUT: n=" << input.schema.size()
              << "  q=" << input.workload.size()
              << "  B=" << input.budget << "\n\n";

    AdvisorResult greedy_res = greedy_advisor(input);
    AdvisorResult dp_res     = dp_advisor(input);

    print_result("Greedy", greedy_res);
    cout << "\n";
    print_result("DP    ", dp_res);

    // Report agreement by savings first, then selection.
    double savings_diff = dp_res.cost_saved - greedy_res.cost_saved;
    bool same_savings = fabs(savings_diff) < 1e-6;  // allow tiny floating-point differences

    unordered_set<string> greedy_keys;
    unordered_set<string> dp_keys;
    for (const auto& c : greedy_res.selected) {
        greedy_keys.insert(c.attr.key());
    }
    for (const auto& c : dp_res.selected) {
        dp_keys.insert(c.attr.key());
    }
    bool same_selection = (greedy_keys == dp_keys);

    if (!same_savings) {
        cout << "\n  *** ALGORITHMS DIVERGE (SAVINGS) ***\n";
        cout << "  DP saves " << fixed << setprecision(2)
                  << savings_diff
                  << " more than Greedy.\n";
    } else if (!same_selection) {
        cout << "\n  (Same cost saved; selections differ)\n";
    } else {
        cout << "\n  (Both algorithms agree on cost saved and selection)\n";
    }

    cout << "\n";
}


/* == TC1: Single attribute - trivial agreement == */

static void tc1() {
    AdvisorInput input;

    input.schema = {
        {"Orders", "customer_id", 10000}
    };
    input.workload = {
        {"SELECT * FROM Orders WHERE customer_id = ?",
         {"Orders.customer_id"}, 100}
    };
    input.budget = 20;

    run_test(
        "TC1: Single attribute - trivial agreement",
        "Both algorithms must select the one available index. Sanity baseline.",
        input
    );
}


/* == TC2: Budget = 0 - graceful empty result == */

static void tc2() {
    AdvisorInput input;

    input.schema = {
        {"Orders", "customer_id", 50000},
        {"Orders", "total",       50000}
    };
    input.workload = {
        {"SELECT * FROM Orders WHERE customer_id = ?", {"Orders.customer_id"}, 100},
        {"SELECT * FROM Orders WHERE total > ?",       {"Orders.total"},       80}
    };
    input.budget = 0;  // nothing can fit

    run_test(
        "TC2: Budget = 0 - graceful empty result",
        "Both algorithms must return empty selection and zero cost savings.",
        input
    );
}


/* == TC3: Greedy diverges - DP finds the better pair == */

static void tc3() {
    AdvisorInput input;

    // Item A: high ratio (greedy picks this first), but alone it's suboptimal
    input.schema.push_back({"T", "a", 6000});
    // Items B and C: lower individual ratios, but together they beat A
    input.schema.push_back({"T", "b", 4000});
    input.schema.push_back({"T", "c", 4000});

    // One query per attribute so each attribute's benefit = 1 * (n - log2(n))
    input.workload = {
        {"Q_a filters T.a", {"T.a"}, 1},
        {"Q_b filters T.b", {"T.b"}, 1},
        {"Q_c filters T.c", {"T.c"}, 1}
    };

    // Budget = 8 units.
    // storage_cost(6000) = 6, storage_cost(4000) = 4.
    // Greedy picks A (cost=6), can't fit B or C (cost=4 each, remaining=2).
    // DP picks B+C (total cost=8), which together beat A.
    input.budget = 8;

    run_test(
        "TC3: Greedy vs DP divergence - B+C pair beats single A",
        "Greedy's high-ratio pick exhausts budget. DP finds the better pair. "
        "DP should save ~33% more than Greedy.",
        input
    );
}


/* == TC4: Small scale baseline (n=5, q=5) == */

static void tc4() {
    AdvisorInput input;

    input.schema = {
        {"R", "f1", 10000}, {"R", "f2", 20000}, {"R", "f3", 30000},
        {"S", "g1", 15000}, {"S", "g2", 25000}
    };
    input.workload = {
        {"Q1", {"R.f1", "R.f2"}, 50},
        {"Q2", {"R.f2", "R.f3"}, 30},
        {"Q3", {"S.g1"},         80},
        {"Q4", {"S.g2", "R.f1"}, 20},
        {"Q5", {"R.f3", "S.g2"}, 60}
    };
    input.budget = 50;

    run_test(
        "TC4: Small scale baseline (n=5, q=5, B=50)",
        "Establish operation counts at small scale. DP table is tiny (5x50). "
        "Both algorithms should be fast and operation counts comparable.",
        input
    );
}


/* == TC5: Medium scale (n=20, q=15) == */

static void tc5() {
    AdvisorInput input;

    // 20 attributes: 5 relations × 4 attributes each
    for (int r = 0; r < 5; r++) {
        for (int f = 0; f < 4; f++) {
            input.schema.push_back({
                "R" + to_string(r),
                "f" + to_string(f),
                (r + 1) * 10000 + f * 3000
            });
        }
    }

    // 15 queries referencing 2-3 attributes each
    vector<vector<string>> refs = {
        {"R0.f0","R0.f1"}, {"R1.f0","R1.f2"}, {"R2.f1","R2.f3"},
        {"R3.f0","R3.f1"}, {"R4.f2","R4.f3"}, {"R0.f2","R1.f1"},
        {"R2.f0","R3.f2"}, {"R4.f0","R0.f3"}, {"R1.f3","R2.f2"},
        {"R3.f3","R4.f1"}, {"R0.f0","R2.f1","R4.f2"},
        {"R1.f0","R3.f1","R0.f2"}, {"R2.f3","R4.f0","R1.f1"},
        {"R3.f2","R0.f1","R2.f0"}, {"R4.f3","R1.f2","R3.f0"}
    };

    for (int i = 0; i < 15; i++) {
        input.workload.push_back({
            "Q" + to_string(i),
            {refs[i].begin(), refs[i].end()},
            20 + i * 5
        });
    }

    input.budget = 80;

    run_test(
        "TC5: Medium scale (n=20, q=15, B=80)",
        "Show O(n*B) vs O(n*q) gap. DP table is 20x80=1600 cells. "
        "Greedy ops ≈ n*q=300; DP ops ≈ n*q + n*B=1900. "
        "num_operations should reflect this ratio.",
        input
    );
}


/* == TC6: All equal ratios - Greedy tie-breaking == */

static void tc6() {
    AdvisorInput input;

    // 10 attributes on the same relation, all identical tuple counts.
    // Every attribute has the same benefit and storage cost => same ratio.
    // Greedy's ordering among them is arbitrary (sort is not stable).
    for (int i = 0; i < 10; i++) {
        input.schema.push_back({"T", "f" + to_string(i), 10000});
        input.workload.push_back({
            "Q" + to_string(i),
            {"T.f" + to_string(i)},
            100
        });
    }

    // storage_cost(10000) = 10. Budget = 35 fits exactly 3 indexes.
    // Both algorithms must select exactly 3, but possibly different ones.
    // DP is deterministic (fills left-to-right); Greedy depends on sort stability.
    input.budget = 35;

    run_test(
        "TC6: All equal benefit-to-cost ratios (n=10, q=10, B=35)",
        "When all ratios are equal, Greedy's pick order is arbitrary. "
        "DP still finds an optimal 3-index subset. Both should save the same amount "
        "even if they pick different indexes. num_operations shows DP doing more work.",
        input
    );
}


/* == TC7: Large scale - Greedy shines (optimal + faster) == */

static void tc7() {
    AdvisorInput input;

    // 60 attributes: 6 relations × 10 attributes each
    for (int r = 0; r < 6; r++) {
        for (int f = 0; f < 10; f++) {
            input.schema.push_back({
                "R" + to_string(r),
                "f" + to_string(f),
                10000
            });
        }
    }

    // One query per attribute, descending frequency
    for (int i = 0; i < 60; i++) {
        string key = "R" + to_string(i / 10) + ".f" + to_string(i % 10);
        input.workload.push_back({
            "Q" + to_string(i),
            {key},
            120 - i
        });
    }

    // storage_cost(10000) = 10, so budget=200 fits 20 indexes
    input.budget = 200;

    run_test(
        "TC7: Large scale - Greedy shines",
        "Equal costs, different benefits. Greedy and DP agree on the top 20 indexes, "
        "but DP pays a larger O(n*B) cost.",
        input
    );
}


/* == TC8: Large scale - DP shines (Greedy is suboptimal) == */

static void tc8() {
    AdvisorInput input;

    // Core divergence pattern: A vs B+C
    input.schema.push_back({"T", "a", 6000});
    input.schema.push_back({"T", "b", 4000});
    input.schema.push_back({"T", "c", 4000});

    // Filler attributes with low ratio to scale n and q
    for (int i = 0; i < 297; i++) {
        input.schema.push_back({"F", "x" + to_string(i), 1000});
    }

    input.workload.push_back({"Q_a", {"T.a"}, 20});
    input.workload.push_back({"Q_b", {"T.b"}, 20});
    input.workload.push_back({"Q_c", {"T.c"}, 20});

    for (int i = 0; i < 297; i++) {
        input.workload.push_back({
            "Q_f" + to_string(i),
            {"F.x" + to_string(i)},
            1
        });
    }

    // Budget = 8 (A costs 6, B and C cost 4 each)
    input.budget = 8;

    run_test(
        "TC8: Large scale - DP shines",
        "Large n and q. Greedy selects high-ratio A; DP picks B+C with a much larger savings gap.",
        input
    );
}


/* == TC9: Large scale - Converge, ops differ (n=100, q=20) == */

static void tc9() {
    AdvisorInput input;

    // 100 attributes: 10 relations × 10 attributes each
    for (int r = 0; r < 10; r++) {
        for (int f = 0; f < 10; f++) {
            input.schema.push_back({
                "R" + to_string(r),
                "f" + to_string(f),
                15000
            });
        }
    }

    // 20 queries, each touching 5 consecutive attributes
    for (int q = 0; q < 20; q++) {
        vector<string> attrs;
        for (int k = 0; k < 5; k++) {
            int idx = q * 5 + k;  // 0..99
            attrs.push_back("R" + to_string(idx / 10) + ".f" + to_string(idx % 10));
        }
        input.workload.push_back({
            "Q" + to_string(q),
            {attrs.begin(), attrs.end()},
            30
        });
    }

    // storage_cost(15000) = 15, budget=160 fits 10 indexes
    input.budget = 160;

    run_test(
        "TC9: Large scale - Converge, ops differ",
        "Uniform benefits mean both algorithms save the same, but DP still does more work.",
        input
    );
}


/* == TC10: Real-world workload (e-commerce platform) == */

static void tc10() {
    AdvisorInput input;

    input.schema = {
        {"Users", "id", 2000000},
        {"Users", "email", 2000000},
        {"Users", "signup_date", 2000000},
        {"Users", "region", 2000000},
        {"Orders", "id", 15000000},
        {"Orders", "user_id", 15000000},
        {"Orders", "created_at", 15000000},
        {"Orders", "status", 15000000},
        {"OrderItems", "order_id", 60000000},
        {"OrderItems", "product_id", 60000000},
        {"OrderItems", "price", 60000000},
        {"Products", "id", 500000},
        {"Products", "category_id", 500000},
        {"Products", "price", 500000},
        {"Products", "inventory", 500000},
        {"Categories", "id", 2000},
        {"Categories", "name", 2000},
        {"Payments", "order_id", 14000000},
        {"Payments", "status", 14000000},
        {"Payments", "method", 14000000}
    };

    input.workload = {
        {"Q1: login by email", {"Users.email"}, 900},
        {"Q2: user profile by id", {"Users.id"}, 1500},
        {"Q3: recent signups by date", {"Users.signup_date"}, 120},
        {"Q4: active users by region", {"Users.region"}, 80},
        {"Q5: orders by user", {"Orders.user_id"}, 1400},
        {"Q6: orders by status", {"Orders.status"}, 500},
        {"Q7: orders by date", {"Orders.created_at"}, 450},
        {"Q8: order details", {"Orders.id", "OrderItems.order_id"}, 1100},
        {"Q9: items by product", {"OrderItems.product_id"}, 700},
        {"Q10: price analytics", {"OrderItems.price"}, 200},
        {"Q11: product detail", {"Products.id"}, 900},
        {"Q12: category browse", {"Products.category_id", "Categories.id"}, 650},
        {"Q13: inventory check", {"Products.inventory"}, 300},
        {"Q14: product price filter", {"Products.price"}, 420},
        {"Q15: payments by order", {"Payments.order_id"}, 800},
        {"Q16: payment status", {"Payments.status"}, 600},
        {"Q17: payment method", {"Payments.method"}, 350}
    };

    // Storage budget sized to allow a selective but not exhaustive index set.
    input.budget = 1200;

    run_test(
        "TC10: Real-world workload (e-commerce)",
        "A production-like workload with realistic tables, query patterns, and frequencies.",
        input
    );
}


/* ─── Main ───────────────────────────────────────────────────────────────── */

int main() {
    cout << "=== Index Selection Advisor - Test Case Runner ===" << "\n\n";

    tc1();
    tc2();
    tc3();
    tc4();
    tc5();
    tc6();
    tc7();
    tc8();
    tc9();
    tc10();

    print_separator();
    cout << "\n\nAll test cases complete.\n\n";
    return 0;
}
