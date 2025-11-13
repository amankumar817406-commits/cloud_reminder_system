// server.cpp
// C++ cross-platform HTTP backend for Cloud Reminder System
// - Routes:
//     GET  /api/reminders
//     POST /api/add       (body: JSON reminder object)
//     POST /api/delete    (body: { "id": "<id>" })
// - Storage: reminders.json (same dir)
// - Single-threaded HTTP server (cpp-httplib) + nlohmann::json
// - Handles CORS (allows your frontend on localhost:5500 to call it)
// Build: g++ server.cpp -std=c++17 -pthread -o server

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <filesystem>

#include "httplib.h"      // https://github.com/yhirose/cpp-httplib (single-header)
#include "json.hpp"       // https://github.com/nlohmann/json (single-header)

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::string DATA_FILE = "reminders.json";
std::mutex file_mutex;

// Simple id generator (timestamp + random)
std::string make_id() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    // a basic id string
    return "id" + std::to_string(ms) + std::to_string(rand() % 10000);
}

// Read reminders.json safely. If file missing, create empty array and return it.
json load_reminders() {
    std::lock_guard<std::mutex> lock(file_mutex);
    if (!fs::exists(DATA_FILE)) {
        // create empty array file
        std::ofstream out(DATA_FILE);
        out << "[]";
        out.close();
        return json::array();
    }
    std::ifstream in(DATA_FILE);
    try {
        json j;
        in >> j;
        if (!j.is_array()) return json::array();
        return j;
    } catch (...) {
        // If parsing fails, return empty array (or you could attempt legacy parsing)
        return json::array();
    }
}

// Atomically write reminders
bool save_reminders(const json &j) {
    std::lock_guard<std::mutex> lock(file_mutex);
    std::string tmp = DATA_FILE + ".tmp";
    std::ofstream out(tmp);
    if (!out.is_open()) return false;
    out << j.dump(2);
    out.close();
    // replace file
    std::error_code ec;
    fs::rename(tmp, DATA_FILE, ec);
    if (ec) {
        // fallback: try overwrite
        std::ofstream out2(DATA_FILE);
        if (!out2.is_open()) return false;
        out2 << j.dump(2);
        out2.close();
    }
    return true;
}

// Helper: add CORS headers to a response
void add_cors(httplib::Response &res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

// Validate reminder object shape (basic). Adjust this to match your frontend fields exactly.
bool valid_reminder_shape(const json &r) {
    // example: expect fields: id (optional), title, day, month, year, time
    if (!r.is_object()) return false;
    if (!r.contains("title")) return false;
    if (!r.contains("day") || !r.contains("month") || !r.contains("year")) return false;
    return true;
}

int main() {
    srand((unsigned)time(nullptr));

    httplib::Server svr;

    // Options handler for preflight CORS requests
    svr.Options(R"(/.*)", [](const httplib::Request &req, httplib::Response &res) {
        add_cors(res);
        res.status = 200;
        res.set_header("Content-Type", "text/plain");
        res.body = "OK";
    });

    // GET /api/reminders  -> return JSON array
    svr.Get("/api/reminders", [](const httplib::Request &req, httplib::Response &res) {
        json reminders = load_reminders();
        add_cors(res);
        res.set_header("Content-Type", "application/json");
        res.set_content(reminders.dump(2), "application/json");
    });

    // POST /api/add -> add reminder JSON body, return added object (with id)
    svr.Post("/api/add", [](const httplib::Request &req, httplib::Response &res) {
        add_cors(res);

        if (req.body.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"empty body"})", "application/json");
            return;
        }

        try {
            json body = json::parse(req.body);
            if (!valid_reminder_shape(body)) {
                res.status = 400;
                res.set_content(R"({"error":"invalid reminder shape"})", "application/json");
                return;
            }

            // assign id if missing
            if (!body.contains("id") || body["id"].is_null() || body["id"].get<std::string>().empty()) {
                body["id"] = make_id();
            }

            json reminders = load_reminders();
            reminders.push_back(body);
            if (!save_reminders(reminders)) {
                res.status = 500;
                res.set_content(R"({"error":"failed to save"})", "application/json");
                return;
            }

            res.status = 200;
            res.set_content(body.dump(2), "application/json");
        } catch (std::exception &e) {
            res.status = 400;
            json err = { {"error", "invalid json"}, {"detail", e.what()} };
            res.set_content(err.dump(2), "application/json");
        }
    });

    // POST /api/delete -> body: { "id": "<id>" }  or { "id": 123 }
    svr.Post("/api/delete", [](const httplib::Request &req, httplib::Response &res) {
        add_cors(res);

        if (req.body.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"empty body"})", "application/json");
            return;
        }
        try {
            json body = json::parse(req.body);
            if (!body.contains("id")) {
                res.status = 400;
                res.set_content(R"({"error":"missing id"})", "application/json");
                return;
            }
            std::string id = body["id"].get<std::string>();

            json reminders = load_reminders();
            bool found = false;
            for (auto it = reminders.begin(); it != reminders.end(); ++it) {
                if (it->contains("id") && (*it)["id"].get<std::string>() == id) {
                    reminders.erase(it);
                    found = true;
                    break;
                }
            }
            if (!found) {
                res.status = 404;
                res.set_content(R"({"error":"id not found"})", "application/json");
                return;
            }
            if (!save_reminders(reminders)) {
                res.status = 500;
                res.set_content(R"({"error":"failed to save after delete"})", "application/json");
                return;
            }
            res.status = 200;
            res.set_content(R"({"ok":true})", "application/json");
        } catch (std::exception &e) {
            res.status = 400;
            json err = { {"error", "invalid json"}, {"detail", e.what()} };
            res.set_content(err.dump(2), "application/json");
        }
    });

    // root quick check
    svr.Get("/", [](const httplib::Request &req, httplib::Response &res) {
        add_cors(res);
        res.set_content(R"({"status":"ok","message":"C++ reminder server running"})", "application/json");
    });

    std::cout << "Starting C++ reminder server on port 8080...\n";
    svr.listen("0.0.0.0", 8080);

    return 0;
}
