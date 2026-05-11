/*
 * main.cpp - Entry point for the Index Advisor server.
 */
#include "handlers.h"

#include <httplib.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

string find_web_root() {
	namespace fs = std::filesystem;
	vector<fs::path> candidates;
	fs::path cwd = fs::current_path();

	candidates.push_back(cwd / "web");
	candidates.push_back(cwd.parent_path() / "web");
	candidates.push_back(cwd.parent_path().parent_path() / "web");

	for (const auto& path : candidates) {
		if (fs::exists(path) && fs::is_directory(path)) {
			return path.string();
		}
	}

	return "";
}

int parse_port(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		string arg = argv[i];
		if (arg == "--port" && i + 1 < argc) {
			return stoi(argv[i + 1]);
		}
	}
	return 8080;
}

string parse_web_root(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		string arg = argv[i];
		if (arg == "--web-root" && i + 1 < argc) {
			return argv[i + 1];
		}
	}
	return "";
}

}  // namespace

int main(int argc, char** argv) {
	int port = parse_port(argc, argv);
	string web_root = parse_web_root(argc, argv);
	if (web_root.empty()) {
		web_root = find_web_root();
	}

	httplib::Server server;
	register_routes(server, web_root);

	if (web_root.empty()) {
		cout << "Warning: web folder not found. API only mode." << endl;
	} else {
		cout << "Serving UI from: " << web_root << endl;
	}
	cout << "Listening on http://localhost:" << port << endl;

	server.listen("0.0.0.0", port);
	return 0;
}
