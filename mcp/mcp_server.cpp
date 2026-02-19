/**
 * Feed MCP Server
 *
 * Model Context Protocol server that exposes feed functionality as tools
 * for AI assistants like Claude.
 *
 * Protocol: JSON-RPC 2.0 over stdio
 * Reference: https://modelcontextprotocol.io/
 */

#include <iostream>
#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "commands.h"

using json = nlohmann::json;

namespace feed {
namespace mcp {

// Tool definition
struct Tool {
    std::string name;
    std::string description;
    json input_schema;
    std::function<std::string(const json&)> handler;
};

class MCPServer {
public:
    MCPServer() {
        register_tools();
    }

    void run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;

            try {
                json request = json::parse(line);
                json response = handle_request(request);

                // Only send response if it's not empty (notifications don't get responses)
                if (!response.empty()) {
                    std::cout << response.dump() << std::endl;
                    std::cout.flush();
                }
            } catch (const json::exception& e) {
                json error_response;
                error_response["jsonrpc"] = "2.0";
                error_response["id"] = nullptr;
                error_response["error"] = {
                    {"code", -32700},
                    {"message", "Parse error"},
                    {"data", e.what()}
                };
                std::cout << error_response.dump() << std::endl;
                std::cout.flush();
            }
        }
    }

private:
    std::unordered_map<std::string, Tool> tools_;
    bool initialized_ = false;

    void register_tools() {
        // get_recent_commits
        tools_["get_recent_commits"] = {
            "get_recent_commits",
            "Get recent commits, optionally filtered by repository. IMPORTANT: Use limit=10 for initial queries to avoid large responses. Increase only if needed.",
            {
                {"type", "object"},
                {"properties", {
                    {"repo", {{"type", "string"}, {"description", "Repository name to filter by (recommended to reduce response size)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max commits to return. Start with 10, increase if needed."}, {"default", 10}}}
                }}
            },
            [](const json& args) -> std::string {
                std::string repo = args.value("repo", "");
                int limit = args.value("limit", 10);
                return commands::get_recent_commits(repo, limit);
            }
        };

        // find_similar_commits
        tools_["find_similar_commits"] = {
            "find_similar_commits",
            "Find commits with similar messages using semantic search (TF-IDF). Returns top matches ranked by similarity.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query (e.g., 'optimize database query')"}}},
                    {"top_k", {{"type", "integer"}, {"description", "Number of results (keep low: 3-5 recommended)"}, {"default", 5}}}
                }},
                {"required", {"query"}}
            },
            [](const json& args) -> std::string {
                std::string query = args.value("query", "");
                int top_k = args.value("top_k", 5);
                return commands::find_similar(query, top_k);
            }
        };

        // get_tagged_commits
        tools_["get_tagged_commits"] = {
            "get_tagged_commits",
            "Get commits by classification tag. IMPORTANT: Use small values for days (7) and limit (10) to avoid large responses.",
            {
                {"type", "object"},
                {"properties", {
                    {"tag", {{"type", "string"}, {"description", "Classification tag"},
                            {"enum", {"bugfix", "optimization", "refactor", "feature", "experimental",
                                     "temporary", "architectural_change", "documentation", "testing",
                                     "security", "dependency"}}}},
                    {"days", {{"type", "integer"}, {"description", "Look back period in days (default 7, max recommended 14)"}, {"default", 7}}},
                    {"limit", {{"type", "integer"}, {"description", "Max commits to return (default 10, increase only if needed)"}, {"default", 10}}}
                }},
                {"required", {"tag"}}
            },
            [](const json& args) -> std::string {
                std::string tag = args.value("tag", "");
                int days = args.value("days", 7);
                int limit = args.value("limit", 10);
                return commands::get_tagged(tag, days, limit);
            }
        };

        // get_repo_summary
        tools_["get_repo_summary"] = {
            "get_repo_summary",
            "Get activity summary stats for a repository. Lightweight - returns aggregated stats, not full commits.",
            {
                {"type", "object"},
                {"properties", {
                    {"repo", {{"type", "string"}, {"description", "Repository name"}}},
                    {"days", {{"type", "integer"}, {"description", "Analysis period in days"}, {"default", 7}}}
                }},
                {"required", {"repo"}}
            },
            [](const json& args) -> std::string {
                std::string repo = args.value("repo", "");
                int days = args.value("days", 7);
                return commands::get_summary(repo, days);
            }
        };

        // list_repos
        tools_["list_repos"] = {
            "list_repos",
            "List repositories matching the current filter configuration",
            {
                {"type", "object"},
                {"properties", json::object()}
            },
            [](const json& args) -> std::string {
                return commands::list_repos();
            }
        };

        // sync_commits
        tools_["sync_commits"] = {
            "sync_commits",
            "Sync commits from GitHub. REQUIRES: init_feed must be called first. Check get_config to verify initialization.",
            {
                {"type", "object"},
                {"properties", json::object()}
            },
            [](const json& args) -> std::string {
                return commands::sync();
            }
        };

        // get_config
        tools_["get_config"] = {
            "get_config",
            "Get current feed configuration. IMPORTANT: Call this first to check if feed is initialized. If error 'Not initialized', use init_feed to set up.",
            {
                {"type", "object"},
                {"properties", json::object()}
            },
            [](const json& args) -> std::string {
                return commands::get_config();
            }
        };

        // init_feed
        tools_["init_feed"] = {
            "init_feed",
            "Initialize feed with a GitHub organization. REQUIRED before sync_commits. Ask user for their org name and optional filters.",
            {
                {"type", "object"},
                {"properties", {
                    {"org", {{"type", "string"}, {"description", "GitHub organization name (required - ask user)"}}},
                    {"languages", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Filter by programming languages (e.g., ['go', 'python'])"}}},
                    {"max_repos", {{"type", "integer"}, {"description", "Maximum repositories to track (recommended: 10-50)"}, {"default", 50}}},
                    {"include_repos", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Specific repo names to include"}}},
                    {"active_days", {{"type", "integer"}, {"description", "Only repos with commits in last N days"}, {"default", 0}}}
                }},
                {"required", {"org"}}
            },
            [](const json& args) -> std::string {
                std::string org = args.value("org", "");
                if (org.empty()) {
                    return "{\"error\": \"Organization name is required. Ask the user which GitHub org they want to track.\"}";
                }

                RepoFilter filter;

                if (args.contains("languages") && args["languages"].is_array()) {
                    for (const auto& lang : args["languages"]) {
                        filter.languages.insert(lang.get<std::string>());
                    }
                }

                if (args.contains("include_repos") && args["include_repos"].is_array()) {
                    for (const auto& repo : args["include_repos"]) {
                        filter.include_repos.insert(repo.get<std::string>());
                    }
                }

                filter.max_repos = args.value("max_repos", 50);
                filter.active_days = args.value("active_days", 0);

                // Token comes from environment variable
                return commands::init(org, "", filter, false);
            }
        };

        // get_available_tags
        tools_["get_available_tags"] = {
            "get_available_tags",
            "List all available classification tags",
            {
                {"type", "object"},
                {"properties", json::object()}
            },
            [](const json& args) -> std::string {
                return commands::get_tags();
            }
        };

        // rebuild_index
        tools_["rebuild_index"] = {
            "rebuild_index",
            "Rebuild the search index (TF-IDF vocabulary and embeddings)",
            {
                {"type", "object"},
                {"properties", json::object()}
            },
            [](const json& args) -> std::string {
                return commands::rebuild_index();
            }
        };
    }

    json handle_request(const json& request) {
        std::string method = request.value("method", "");

        // Handle notifications (no id, no response)
        if (method == "initialized" || method == "notifications/initialized") {
            // Client acknowledges initialization - no response needed
            return json();
        }

        // All other methods require a response
        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request.value("id", json(nullptr));

        if (method == "initialize") {
            return handle_initialize(request);
        } else if (method == "tools/list") {
            return handle_tools_list(request);
        } else if (method == "tools/call") {
            return handle_tools_call(request);
        } else if (method == "ping") {
            response["result"] = json::object();
            return response;
        } else {
            response["error"] = {
                {"code", -32601},
                {"message", "Method not found"},
                {"data", method}
            };
        }

        return response;
    }

    json handle_initialize(const json& request) {
        initialized_ = true;

        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request.value("id", json(nullptr));

        // Build capabilities object
        json capabilities = json::object();
        capabilities["tools"] = json::object();  // We support tools

        response["result"] = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", capabilities},
            {"serverInfo", {
                {"name", "feed-mcp"},
                {"version", "1.0.0"}
            }}
        };

        return response;
    }

    json handle_tools_list(const json& request) {
        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request.value("id", json(nullptr));

        json tools_array = json::array();
        for (const auto& [name, tool] : tools_) {
            tools_array.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            });
        }

        response["result"] = {{"tools", tools_array}};
        return response;
    }

    json handle_tools_call(const json& request) {
        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request.value("id", json(nullptr));

        if (!request.contains("params") || !request["params"].contains("name")) {
            response["error"] = {
                {"code", -32602},
                {"message", "Invalid params: missing tool name"}
            };
            return response;
        }

        std::string tool_name = request["params"]["name"];
        json arguments = request["params"].value("arguments", json::object());

        auto it = tools_.find(tool_name);
        if (it == tools_.end()) {
            response["error"] = {
                {"code", -32602},
                {"message", "Unknown tool: " + tool_name}
            };
            return response;
        }

        try {
            std::string result = it->second.handler(arguments);

            // Parse the result to check for errors
            json result_json = json::parse(result);

            if (result_json.contains("error")) {
                // Tool returned an error
                response["result"] = {
                    {"content", {{
                        {"type", "text"},
                        {"text", result}
                    }}},
                    {"isError", true}
                };
            } else {
                response["result"] = {
                    {"content", {{
                        {"type", "text"},
                        {"text", result}
                    }}}
                };
            }
        } catch (const std::exception& e) {
            response["result"] = {
                {"content", {{
                    {"type", "text"},
                    {"text", std::string("{\"error\": \"") + e.what() + "\"}"}
                }}},
                {"isError", true}
            };
        }

        return response;
    }
};

}  // namespace mcp
}  // namespace feed

int main() {
    // Disable buffering for real-time communication
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);

    feed::mcp::MCPServer server;
    server.run();

    return 0;
}
