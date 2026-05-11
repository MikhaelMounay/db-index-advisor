/*
 * handlers.h - HTTP route handlers for the Index Advisor server.
 */
#pragma once

#include <string>

namespace httplib {
class Server;
}

void register_routes(httplib::Server& server, const std::string& web_root);
