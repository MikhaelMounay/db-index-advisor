/*
 * handlers.cpp - HTTP route handlers and JSON parsing.
 */
#include "handlers.h"

#include "../core/cost_model.h"
#include "../core/dp.h"
#include "../core/greedy.h"
#include "../core/types.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
using namespace std;

namespace {

struct Example {
	string id;
	string name;
	string description;
	AdvisorInput input;
};

json candidate_to_json(const CandidateIndex& c) {
	json j;
	j["key"] = c.attr.key();
	j["relation"] = c.attr.relation;
	j["name"] = c.attr.name;
	j["tuples"] = c.attr.tuples;
	j["storage_cost"] = c.storage_cost;
	j["benefit"] = c.benefit;
	j["ratio"] = (c.storage_cost > 0) ? (c.benefit / static_cast<double>(c.storage_cost)) : 0.0;
	return j;
}

long long sum_storage(const vector<CandidateIndex>& selected) {
	long long total = 0;
	for (const auto& c : selected) {
		total += c.storage_cost;
	}
	return total;
}

json result_to_json(const AdvisorResult& r, int budget) {
	json j;
	j["cost_before"] = r.cost_before;
	j["cost_after"] = r.cost_after;
	j["cost_saved"] = r.cost_saved;
	j["runtime_us"] = r.runtime_us;
	j["num_operations"] = r.num_operations;

	vector<CandidateIndex> selected = r.selected;
	reverse(selected.begin(), selected.end());

	json selected_json = json::array();
	unordered_set<string> selected_keys;
	for (const auto& c : selected) {
		selected_json.push_back(candidate_to_json(c));
		selected_keys.insert(c.attr.key());
	}

	long long used = sum_storage(selected);
	j["budget_used"] = used;
	j["budget_remaining"] = max(0, budget - static_cast<int>(used));
	j["selected"] = selected_json;
	j["selected_keys"] = json::array();
	for (const auto& key : selected_keys) {
		j["selected_keys"].push_back(key);
	}
	return j;
}

bool parse_input(const json& body, AdvisorInput& input, string& error) {
	if (!body.is_object()) {
		error = "Body must be a JSON object.";
		return false;
	}

	if (!body.contains("schema") || !body.contains("workload") || !body.contains("budget")) {
		error = "Missing required fields: schema, workload, budget.";
		return false;
	}

	if (!body.at("schema").is_array() || !body.at("workload").is_array()) {
		error = "Schema and workload must be arrays.";
		return false;
	}

	if (!body.at("budget").is_number_integer()) {
		error = "Budget must be an integer.";
		return false;
	}

	input.schema.clear();
	input.workload.clear();
	input.budget = body.at("budget").get<int>();
	if (input.budget < 0) {
		error = "Budget must be non-negative.";
		return false;
	}

	for (const auto& item : body.at("schema")) {
		if (!item.is_object()) {
			error = "Each schema entry must be an object.";
			return false;
		}
		if (!item.contains("relation") || !item.contains("name") || !item.contains("tuples")) {
			error = "Schema entries require relation, name, tuples.";
			return false;
		}
		if (!item.at("relation").is_string() || !item.at("name").is_string()) {
			error = "Schema relation/name must be strings.";
			return false;
		}
		if (!item.at("tuples").is_number_integer()) {
			error = "Schema tuples must be integers.";
			return false;
		}

		Attribute attr;
		attr.relation = item.at("relation").get<string>();
		attr.name = item.at("name").get<string>();
		attr.tuples = item.at("tuples").get<int>();
		if (attr.relation.empty() || attr.name.empty() || attr.tuples <= 0) {
			error = "Schema entries must have non-empty names and positive tuples.";
			return false;
		}
		input.schema.push_back(attr);
	}

	for (const auto& item : body.at("workload")) {
		if (!item.is_object()) {
			error = "Each workload entry must be an object.";
			return false;
		}
		if (!item.contains("label") || !item.contains("attributes") || !item.contains("frequency")) {
			error = "Workload entries require label, attributes, frequency.";
			return false;
		}
		if (!item.at("label").is_string() || !item.at("attributes").is_array()) {
			error = "Workload label must be a string and attributes must be an array.";
			return false;
		}
		if (!item.at("frequency").is_number_integer()) {
			error = "Workload frequency must be an integer.";
			return false;
		}

		Query q;
		q.label = item.at("label").get<string>();
		q.frequency = item.at("frequency").get<int>();
		if (q.label.empty() || q.frequency <= 0) {
			error = "Workload entries must have non-empty labels and positive frequency.";
			return false;
		}

		for (const auto& attr : item.at("attributes")) {
			if (!attr.is_string()) {
				error = "Workload attributes must be strings.";
				return false;
			}
			string key = attr.get<string>();
			if (!key.empty()) {
				q.attributes.insert(key);
			}
		}

		if (q.attributes.empty()) {
			error = "Each workload entry must reference at least one attribute.";
			return false;
		}

		input.workload.push_back(q);
	}

	return true;
}

vector<Example> build_examples() {
	vector<Example> examples;

	Example tc1;
	tc1.id = "tc1";
	tc1.name = "TC1: Single attribute";
	tc1.description = "Trivial agreement with one attribute and one query.";
	tc1.input.schema = { {"Orders", "customer_id", 10000} };
	tc1.input.workload = { {"SELECT * FROM Orders WHERE customer_id = ?", {"Orders.customer_id"}, 100} };
	tc1.input.budget = 20;
	examples.push_back(tc1);

	Example tc2;
	tc2.id = "tc2";
	tc2.name = "TC2: Budget zero";
	tc2.description = "Budget is zero, selection must be empty.";
	tc2.input.schema = { {"Orders", "customer_id", 50000}, {"Orders", "total", 50000} };
	tc2.input.workload = {
		{"SELECT * FROM Orders WHERE customer_id = ?", {"Orders.customer_id"}, 100},
		{"SELECT * FROM Orders WHERE total > ?", {"Orders.total"}, 80}
	};
	tc2.input.budget = 0;
	examples.push_back(tc2);

	Example tc3;
	tc3.id = "tc3";
	tc3.name = "TC3: Greedy diverges";
	tc3.description = "Greedy picks a high ratio index, DP finds the better pair.";
	tc3.input.schema = { {"T", "a", 6000}, {"T", "b", 4000}, {"T", "c", 4000} };
	tc3.input.workload = {
		{"Q_a filters T.a", {"T.a"}, 1},
		{"Q_b filters T.b", {"T.b"}, 1},
		{"Q_c filters T.c", {"T.c"}, 1}
	};
	tc3.input.budget = 8;
	examples.push_back(tc3);

	Example tc4;
	tc4.id = "tc4";
	tc4.name = "TC4: Small scale";
	tc4.description = "Small baseline with n=5, q=5.";
	tc4.input.schema = {
		{"R", "f1", 10000}, {"R", "f2", 20000}, {"R", "f3", 30000},
		{"S", "g1", 15000}, {"S", "g2", 25000}
	};
	tc4.input.workload = {
		{"Q1", {"R.f1", "R.f2"}, 50},
		{"Q2", {"R.f2", "R.f3"}, 30},
		{"Q3", {"S.g1"}, 80},
		{"Q4", {"S.g2", "R.f1"}, 20},
		{"Q5", {"R.f3", "S.g2"}, 60}
	};
	tc4.input.budget = 50;
	examples.push_back(tc4);

	Example tc5;
	tc5.id = "tc5";
	tc5.name = "TC5: Medium scale";
	tc5.description = "n=20, q=15, DP vs Greedy scaling gap.";
	for (int r = 0; r < 5; r++) {
		for (int f = 0; f < 4; f++) {
			tc5.input.schema.push_back({
				"R" + to_string(r),
				"f" + to_string(f),
				(r + 1) * 10000 + f * 3000
			});
		}
	}

	vector<vector<string>> refs = {
		{"R0.f0","R0.f1"}, {"R1.f0","R1.f2"}, {"R2.f1","R2.f3"},
		{"R3.f0","R3.f1"}, {"R4.f2","R4.f3"}, {"R0.f2","R1.f1"},
		{"R2.f0","R3.f2"}, {"R4.f0","R0.f3"}, {"R1.f3","R2.f2"},
		{"R3.f3","R4.f1"}, {"R0.f0","R2.f1","R4.f2"},
		{"R1.f0","R3.f1","R0.f2"}, {"R2.f3","R4.f0","R1.f1"},
		{"R3.f2","R0.f1","R2.f0"}, {"R4.f3","R1.f2","R3.f0"}
	};

	for (int i = 0; i < 15; i++) {
		tc5.input.workload.push_back({
			"Q" + to_string(i),
			{refs[i].begin(), refs[i].end()},
			20 + i * 5
		});
	}
	tc5.input.budget = 80;
	examples.push_back(tc5);

	Example tc6;
	tc6.id = "tc6";
	tc6.name = "TC6: Equal ratios";
	tc6.description = "All equal ratios, Greedy tie-breaks but cost saved matches.";
	for (int i = 0; i < 10; i++) {
		tc6.input.schema.push_back({"T", "f" + to_string(i), 10000});
		tc6.input.workload.push_back({
			"Q" + to_string(i),
			{"T.f" + to_string(i)},
			100
		});
	}
	tc6.input.budget = 35;
	examples.push_back(tc6);

	Example tc7;
	tc7.id = "tc7";
	tc7.name = "TC7: Greedy shines";
	tc7.description = "Large scale where Greedy and DP agree, but DP does more work.";
	for (int r = 0; r < 6; r++) {
		for (int f = 0; f < 10; f++) {
			tc7.input.schema.push_back({
				"R" + to_string(r),
				"f" + to_string(f),
				10000
			});
		}
	}
	for (int i = 0; i < 60; i++) {
		string key = "R" + to_string(i / 10) + ".f" + to_string(i % 10);
		tc7.input.workload.push_back({
			"Q" + to_string(i),
			{key},
			120 - i
		});
	}
	tc7.input.budget = 200;
	examples.push_back(tc7);

	Example tc8;
	tc8.id = "tc8";
	tc8.name = "TC8: DP shines";
	tc8.description = "Large scale where Greedy misses the better pair, DP wins by a much wider margin.";
	tc8.input.schema.push_back({"T", "a", 6000});
	tc8.input.schema.push_back({"T", "b", 4000});
	tc8.input.schema.push_back({"T", "c", 4000});
	for (int i = 0; i < 297; i++) {
		tc8.input.schema.push_back({"F", "x" + to_string(i), 1000});
	}
	tc8.input.workload.push_back({"Q_a", {"T.a"}, 20});
	tc8.input.workload.push_back({"Q_b", {"T.b"}, 20});
	tc8.input.workload.push_back({"Q_c", {"T.c"}, 20});
	for (int i = 0; i < 297; i++) {
		tc8.input.workload.push_back({
			"Q_f" + to_string(i),
			{"F.x" + to_string(i)},
			1
		});
	}
	tc8.input.budget = 8;
	examples.push_back(tc8);

	Example tc9;
	tc9.id = "tc9";
	tc9.name = "TC9: Converge, ops differ";
	tc9.description = "Uniform benefits yield the same savings; DP still does more work.";
	for (int r = 0; r < 10; r++) {
		for (int f = 0; f < 10; f++) {
			tc9.input.schema.push_back({
				"R" + to_string(r),
				"f" + to_string(f),
				15000
			});
		}
	}
	for (int q = 0; q < 20; q++) {
		vector<string> attrs;
		for (int k = 0; k < 5; k++) {
			int idx = q * 5 + k;
			attrs.push_back("R" + to_string(idx / 10) + ".f" + to_string(idx % 10));
		}
		tc9.input.workload.push_back({
			"Q" + to_string(q),
			{attrs.begin(), attrs.end()},
			30
		});
	}
	tc9.input.budget = 160;
	examples.push_back(tc9);

	Example tc10;
	tc10.id = "tc10";
	tc10.name = "TC10: Real-world workload";
	tc10.description = "E-commerce workload with realistic relations, volumes, and query mix.";
	tc10.input.schema = {
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

	tc10.input.workload = {
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

	tc10.input.budget = 1200;
	examples.push_back(tc10);

	return examples;
}

json input_to_json(const AdvisorInput& input) {
	json j;
	j["budget"] = input.budget;
	j["schema"] = json::array();
	for (const auto& attr : input.schema) {
		j["schema"].push_back({
			{"relation", attr.relation},
			{"name", attr.name},
			{"tuples", attr.tuples}
		});
	}
	j["workload"] = json::array();
	for (const auto& q : input.workload) {
		json attrs = json::array();
		for (const auto& key : q.attributes) {
			attrs.push_back(key);
		}
		j["workload"].push_back({
			{"label", q.label},
			{"attributes", attrs},
			{"frequency", q.frequency}
		});
	}
	return j;
}

}  // namespace

void register_routes(httplib::Server& server, const string& web_root) {
	auto examples = make_shared<vector<Example>>(build_examples());

	server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("{\"status\":\"ok\"}", "application/json");
	});

	server.Get("/api/examples", [examples](const httplib::Request&, httplib::Response& res) {
		json list = json::array();
		for (const auto& ex : *examples) {
			list.push_back({
				{"id", ex.id},
				{"name", ex.name},
				{"description", ex.description}
			});
		}
		res.set_content(list.dump(2), "application/json");
	});

	server.Get(R"(/api/examples/([A-Za-z0-9_-]+))",
		[examples](const httplib::Request& req, httplib::Response& res) {
			string id = req.matches[1];
			for (const auto& ex : *examples) {
				if (ex.id == id) {
					json payload;
					payload["id"] = ex.id;
					payload["name"] = ex.name;
					payload["description"] = ex.description;
					payload["input"] = input_to_json(ex.input);
					res.set_content(payload.dump(2), "application/json");
					return;
				}
			}
			res.status = 404;
			res.set_content("{\"error\":\"Example not found\"}", "application/json");
		});

	server.Post("/api/advise", [](const httplib::Request& req, httplib::Response& res) {
		json body;
		try {
			body = json::parse(req.body);
		} catch (const json::parse_error&) {
			res.status = 400;
			res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
			return;
		}

		AdvisorInput input;
		string error;
		if (!parse_input(body, input, error)) {
			res.status = 400;
			json err;
			err["error"] = error;
			res.set_content(err.dump(2), "application/json");
			return;
		}

		AdvisorResult greedy = greedy_advisor(input);
		AdvisorResult dp = dp_advisor(input);

		json candidates = json::array();
		auto candidate_list = build_candidates(input);
		for (const auto& c : candidate_list) {
			candidates.push_back(candidate_to_json(c));
		}

		json payload;
		payload["input"] = input_to_json(input);
		payload["candidates"] = candidates;
		payload["greedy"] = result_to_json(greedy, input.budget);
		payload["dp"] = result_to_json(dp, input.budget);
		payload["meta"] = {
			{"budget", input.budget},
			{"candidate_count", candidate_list.size()}
		};

		res.set_content(payload.dump(2), "application/json");
	});

	if (!web_root.empty()) {
		server.set_mount_point("/", web_root.c_str());
	}
}
