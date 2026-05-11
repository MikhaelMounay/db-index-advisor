const schemaRows = document.getElementById("schema-rows");
const workloadRows = document.getElementById("workload-rows");
const addSchemaBtn = document.getElementById("add-schema");
const addWorkloadBtn = document.getElementById("add-workload");
const runBtn = document.getElementById("run-analysis");
const resetBtn = document.getElementById("reset-workload");
const requestPreview = document.getElementById("request-preview");
const budgetInput = document.getElementById("budget-input");
const exampleSelect = document.getElementById("example-select");
const exampleDescription = document.getElementById("example-description");
const loadExampleBtn = document.getElementById("load-example");
const statusLine = document.getElementById("status-line");

const greedySummary = document.getElementById("greedy-summary");
const dpSummary = document.getElementById("dp-summary");
const greedySelected = document.getElementById("greedy-selected");
const dpSelected = document.getElementById("dp-selected");

let chartCost;
let chartRuntime;
let chartBudget;
let chartCandidates;
let examplesCache = [];

Chart.defaults.font.family = "Space Grotesk, sans-serif";
Chart.defaults.color = "#1b3a4b";
Chart.defaults.devicePixelRatio = window.devicePixelRatio || 1;

function createInput(type, value = "") {
	const input = document.createElement("input");
	input.type = type;
	input.value = value;
	return input;
}

function createSchemaRow({ relation = "", name = "", tuples = 10000 } = {}) {
	const row = document.createElement("tr");
	const relationCell = document.createElement("td");
	const nameCell = document.createElement("td");
	const tuplesCell = document.createElement("td");
	const actionCell = document.createElement("td");

	const relationInput = createInput("text", relation);
	const nameInput = createInput("text", name);
	const tuplesInput = createInput("number", tuples);
	tuplesInput.min = 1;

	relationCell.appendChild(relationInput);
	nameCell.appendChild(nameInput);
	tuplesCell.appendChild(tuplesInput);

	const removeBtn = document.createElement("button");
	removeBtn.className = "ghost";
	removeBtn.textContent = "Remove";
	removeBtn.addEventListener("click", () => {
		row.remove();
		updatePreview();
	});
	actionCell.appendChild(removeBtn);

	row.appendChild(relationCell);
	row.appendChild(nameCell);
	row.appendChild(tuplesCell);
	row.appendChild(actionCell);
	schemaRows.appendChild(row);
}

function createWorkloadRow({ label = "", attributes = "", frequency = 1 } = {}) {
	const row = document.createElement("tr");
	const labelCell = document.createElement("td");
	const attrsCell = document.createElement("td");
	const freqCell = document.createElement("td");
	const actionCell = document.createElement("td");

	const labelInput = createInput("text", label);
	const attrsInput = createInput("text", attributes);
	const freqInput = createInput("number", frequency);
	freqInput.min = 1;

	labelCell.appendChild(labelInput);
	attrsCell.appendChild(attrsInput);
	freqCell.appendChild(freqInput);

	const removeBtn = document.createElement("button");
	removeBtn.className = "ghost";
	removeBtn.textContent = "Remove";
	removeBtn.addEventListener("click", () => {
		row.remove();
		updatePreview();
	});
	actionCell.appendChild(removeBtn);

	row.appendChild(labelCell);
	row.appendChild(attrsCell);
	row.appendChild(freqCell);
	row.appendChild(actionCell);
	workloadRows.appendChild(row);
}

function readSchema() {
	const rows = Array.from(schemaRows.querySelectorAll("tr"));
	return rows.map((row) => {
		const inputs = row.querySelectorAll("input");
		return {
			relation: inputs[0].value.trim(),
			name: inputs[1].value.trim(),
			tuples: Number(inputs[2].value || 0),
		};
	}).filter((item) => item.relation && item.name && item.tuples > 0);
}

function readWorkload() {
	const rows = Array.from(workloadRows.querySelectorAll("tr"));
	return rows.map((row) => {
		const inputs = row.querySelectorAll("input");
		const attrs = inputs[1].value
			.split(",")
			.map((value) => value.trim())
			.filter(Boolean);
		return {
			label: inputs[0].value.trim(),
			attributes: attrs,
			frequency: Number(inputs[2].value || 0),
		};
	}).filter((item) => item.label && item.attributes.length && item.frequency > 0);
}

function buildPayload() {
	return {
		schema: readSchema(),
		workload: readWorkload(),
		budget: Number(budgetInput.value || 0),
	};
}

function updatePreview() {
	const payload = buildPayload();
	requestPreview.textContent = JSON.stringify(payload, null, 2);
}

function setStatus(message) {
	statusLine.textContent = message || "";
}

function formatNumber(value, digits = 2) {
	if (typeof value !== "number") return "-";
	return value.toFixed(digits);
}

function formatInt(value) {
	if (typeof value !== "number") return "-";
	return Math.round(value).toString();
}

function renderSummary(target, data) {
	const items = [
		["Cost before", formatNumber(data.cost_before)],
		["Cost after", formatNumber(data.cost_after)],
		["Cost saved", formatNumber(data.cost_saved)],
		["Budget used", `${formatInt(data.budget_used)} / ${formatInt(data.budget_used + data.budget_remaining)}`],
		["Runtime", `${formatInt(data.runtime_us)} us`],
		["Operations", formatInt(data.num_operations)],
	];

	target.innerHTML = "";
	items.forEach(([label, value]) => {
		const li = document.createElement("li");
		li.innerHTML = `<strong>${label}:</strong> ${value}`;
		target.appendChild(li);
	});
}

function renderSelected(target, selected) {
	target.innerHTML = "";
	if (!selected.length) {
		const li = document.createElement("li");
		li.textContent = "(none)";
		target.appendChild(li);
		return;
	}
	selected.forEach((item) => {
		const li = document.createElement("li");
		li.textContent = `${item.key} | cost=${item.storage_cost}, benefit=${formatNumber(item.benefit)}`;
		target.appendChild(li);
	});
}

function initCharts() {
	const costCtx = document.getElementById("chart-cost");
	const runtimeCtx = document.getElementById("chart-runtime");
	const budgetCtx = document.getElementById("chart-budget");
	const candidatesCtx = document.getElementById("chart-candidates");

	if (chartCost) chartCost.destroy();
	if (chartRuntime) chartRuntime.destroy();
	if (chartBudget) chartBudget.destroy();
	if (chartCandidates) chartCandidates.destroy();

	chartCost = new Chart(costCtx, {
		type: "bar",
		data: {
			labels: ["Cost before", "Cost after", "Cost saved"],
			datasets: [],
		},
		options: {
			responsive: true,
			maintainAspectRatio: false,
			plugins: { legend: { position: "bottom" } },
			scales: {
				y: { beginAtZero: true, grid: { color: "rgba(27, 58, 75, 0.08)" } },
				x: { grid: { display: false } },
			},
		},
	});

	chartRuntime = new Chart(runtimeCtx, {
		type: "bar",
		data: {
			labels: ["Runtime (us)", "Operations"],
			datasets: [],
		},
		options: {
			responsive: true,
			maintainAspectRatio: false,
			plugins: { legend: { position: "bottom" } },
			scales: {
				y: { beginAtZero: true, grid: { color: "rgba(27, 58, 75, 0.08)" } },
				x: { grid: { display: false } },
			},
		},
	});

	chartBudget = new Chart(budgetCtx, {
		type: "doughnut",
		data: {
			labels: ["Used", "Remaining"],
			datasets: [],
		},
		options: {
			responsive: true,
			maintainAspectRatio: false,
			cutout: "70%",
			plugins: { legend: { position: "bottom" } },
		},
	});

	chartCandidates = new Chart(candidatesCtx, {
		type: "bar",
		data: {
			labels: [],
			datasets: [],
		},
		options: {
			responsive: true,
			maintainAspectRatio: false,
			scales: {
				y: { beginAtZero: true, grid: { color: "rgba(27, 58, 75, 0.08)" } },
				x: { ticks: { maxRotation: 45, minRotation: 45 }, grid: { display: false } },
			},
			plugins: { legend: { position: "bottom" } },
		},
	});
}

function updateCharts(result) {
	chartCost.data.datasets = [
		{
			label: "Greedy",
			data: [result.greedy.cost_before, result.greedy.cost_after, result.greedy.cost_saved],
			backgroundColor: "#c56b3f",
		},
		{
			label: "DP",
			data: [result.dp.cost_before, result.dp.cost_after, result.dp.cost_saved],
			backgroundColor: "#1c7c74",
		},
	];
	chartCost.update();

	chartRuntime.data.datasets = [
		{
			label: "Greedy",
			data: [result.greedy.runtime_us, result.greedy.num_operations],
			backgroundColor: "#c56b3f",
		},
		{
			label: "DP",
			data: [result.dp.runtime_us, result.dp.num_operations],
			backgroundColor: "#1c7c74",
		},
	];
	chartRuntime.update();

	chartBudget.data.datasets = [
		{
			label: "Greedy budget",
			data: [result.greedy.budget_used, result.greedy.budget_remaining],
			backgroundColor: ["#c56b3f", "#f3d1ba"],
		},
		{
			label: "DP budget",
			data: [result.dp.budget_used, result.dp.budget_remaining],
			backgroundColor: ["#1c7c74", "#cfe8e5"],
		},
	];
	chartBudget.update();

	const candidates = [...result.candidates]
		.sort((a, b) => b.benefit - a.benefit)
		.slice(0, 10);

	const greedyKeys = new Set(result.greedy.selected_keys || []);
	const dpKeys = new Set(result.dp.selected_keys || []);
	const colors = candidates.map((item) => {
		const inGreedy = greedyKeys.has(item.key);
		const inDp = dpKeys.has(item.key);
		if (inGreedy && inDp) return "#1b3a4b";
		if (inGreedy) return "#c56b3f";
		if (inDp) return "#1c7c74";
		return "#c4bbb0";
	});

	chartCandidates.data.labels = candidates.map((item) => item.key);
	chartCandidates.data.datasets = [
		{
			label: "Benefit (top 10)",
			data: candidates.map((item) => item.benefit),
			backgroundColor: colors,
		},
	];
	chartCandidates.update();
}

function clearResults() {
	greedySummary.innerHTML = "";
	dpSummary.innerHTML = "";
	greedySelected.innerHTML = "";
	dpSelected.innerHTML = "";

	chartCost.data.datasets = [];
	chartRuntime.data.datasets = [];
	chartBudget.data.datasets = [];
	chartCandidates.data.datasets = [];
	chartCandidates.data.labels = [];

	chartCost.update();
	chartRuntime.update();
	chartBudget.update();
	chartCandidates.update();
}

async function loadExamples() {
	const response = await fetch("/api/examples");
	const data = await response.json();
	examplesCache = data;

	exampleSelect.innerHTML = "";
	data.forEach((item, index) => {
		const option = document.createElement("option");
		option.value = item.id;
		option.textContent = item.name;
		if (index === 0) option.selected = true;
		exampleSelect.appendChild(option);
	});

	if (data.length) {
		exampleDescription.textContent = data[0].description;
	}
}

async function loadExampleById(id) {
	const response = await fetch(`/api/examples/${id}`);
	if (!response.ok) {
		setStatus("Failed to load example.");
		return;
	}
	const data = await response.json();
	exampleDescription.textContent = data.description;

	schemaRows.innerHTML = "";
	workloadRows.innerHTML = "";

	data.input.schema.forEach((item) => createSchemaRow(item));
	data.input.workload.forEach((item) => {
		createWorkloadRow({
			label: item.label,
			attributes: item.attributes.join(", "),
			frequency: item.frequency,
		});
	});
	budgetInput.value = data.input.budget;
	updatePreview();
}

async function runAnalysis() {
	const payload = buildPayload();
	updatePreview();

	setStatus("Running analysis...");
	const response = await fetch("/api/advise", {
		method: "POST",
		headers: { "Content-Type": "application/json" },
		body: JSON.stringify(payload),
	});

	if (!response.ok) {
		const error = await response.json();
		setStatus(error.error || "Server error.");
		return;
	}

	const result = await response.json();
	setStatus("Analysis complete.");

	renderSummary(greedySummary, result.greedy);
	renderSummary(dpSummary, result.dp);
	renderSelected(greedySelected, result.greedy.selected || []);
	renderSelected(dpSelected, result.dp.selected || []);
	updateCharts(result);
}

function resetWorkspace() {
	schemaRows.innerHTML = "";
	workloadRows.innerHTML = "";
	createSchemaRow({ relation: "Orders", name: "customer_id", tuples: 10000 });
	createSchemaRow({ relation: "Orders", name: "total", tuples: 50000 });
	createWorkloadRow({
		label: "Q1",
		attributes: "Orders.customer_id",
		frequency: 100,
	});
	createWorkloadRow({
		label: "Q2",
		attributes: "Orders.total",
		frequency: 80,
	});
	budgetInput.value = 20;
	updatePreview();
	clearResults();
	setStatus("Workspace reset.");
}

schemaRows.addEventListener("input", updatePreview);
workloadRows.addEventListener("input", updatePreview);
budgetInput.addEventListener("input", updatePreview);

addSchemaBtn.addEventListener("click", () => createSchemaRow());
addWorkloadBtn.addEventListener("click", () => createWorkloadRow());
runBtn.addEventListener("click", runAnalysis);
resetBtn.addEventListener("click", resetWorkspace);

exampleSelect.addEventListener("change", (event) => {
	const selectedId = event.target.value;
	const match = examplesCache.find((item) => item.id === selectedId);
	if (match) {
		exampleDescription.textContent = match.description;
	}
});

loadExampleBtn.addEventListener("click", () => {
	loadExampleById(exampleSelect.value);
});

initCharts();
resetWorkspace();
loadExamples();
