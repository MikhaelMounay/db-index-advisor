# Database Index Selection Advisor

Course project for **CSCE2203 - Analysis and Design of Algorithms Lab (Spring 2026)** at AUC.

This project implements and compares two algorithmic approaches to the database index selection problem:
- Greedy selection by benefit-to-cost ratio
- Optimal selection via 0/1 knapsack dynamic programming (DP)

It includes a C++ core library, a test runner with curated cases, and a local web UI for side-by-side analysis.

## Features

- Shared cost model for fair comparison
- Greedy vs DP selection, cost, runtime, and operation counts
- Curated test cases (small, medium, large, and real-world inspired)
- Local HTTP server with a modern UI and charts

## Project Layout

- Core algorithms: [src/core](src/core)
- HTTP server: [src/server](src/server)
- Test runner: [src/tests/test_cases.cpp](src/tests/test_cases.cpp)
- Web UI: [web](web)

## Build (WSL recommended)

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Outputs:
- Test runner: `build/bin/tests`
- Server: `build/bin/server`

Note: The server uses `cpp-httplib` and `nlohmann_json` via CMake FetchContent.

## Run Tests

```bash
./build/bin/tests
```

This prints selected indexes, costs, operation counts, and runtime for each test case.

## Run the Server + UI

```bash
./build/bin/server --port 8080
```

Then open:
`http://localhost:8080`

### API Endpoints

- `GET /api/health`
- `GET /api/examples`
- `GET /api/examples/{id}`
- `POST /api/advise`

Example request body for `/api/advise`:

```json
{
	"schema": [
		{"relation": "Orders", "name": "customer_id", "tuples": 10000}
	],
	"workload": [
		{
			"label": "Q1",
			"attributes": ["Orders.customer_id"],
			"frequency": 100
		}
	],
	"budget": 20
}
```

## Notes on the Cost Model

- Full scan cost: $n$
- Index scan cost: $\log_2(n)$
- Storage cost per index: $\max(1, \lfloor n/1000 \rfloor)$

These simplifications make algorithmic comparison clear and reproducible while keeping the model transparent.
