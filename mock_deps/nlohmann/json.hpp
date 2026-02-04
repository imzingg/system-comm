#pragma once
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>

namespace nlohmann {
    struct json {
        std::map<std::string, std::string> internal_map;

        struct Any {
            template<typename T> Any(const T&) {}
        };

        json() {}
        template <typename T>
        json(const T&) {}

        json(std::initializer_list<std::pair<const char*, Any>> l) {
            // Simplified: just mock it. Real serialization is hard to mock perfectly in 10 lines.
            // We just need .dump() to return something not empty if checked.
        }

        // Improve dump for testing "send_status"
        std::string dump() const {
            if (internal_map.count("state")) {
                std::stringstream ss;
                ss << "{\"state\": \"" << internal_map.at("state") << "\"}";
                return ss.str();
            }
            return "{}";
        }

        static json parse(const std::string& s) {
            json j;
            if (s.find("state") != std::string::npos) {
                 if (s.find("START") != std::string::npos) j.internal_map["state"] = "START";
                 if (s.find("COMPLETED") != std::string::npos) j.internal_map["state"] = "COMPLETED";
                 if (s.find("ERROR") != std::string::npos) j.internal_map["state"] = "ERROR";
            }
            return j;
        }

        struct Val {
            std::string s_val;
            operator std::string() const { return s_val; }
            operator int() const { return 0; }
            operator size_t() const { return 0; }

            template<typename T>
            Val& operator=(const T& v) { return *this; }

            // Special case for string assignment to capture state in mock
            Val& operator=(const char* v) { s_val = v; return *this; }
            Val& operator=(const std::string& v) { s_val = v; return *this; }
        };

        Val operator[](const char* key) {
            if (internal_map.count(key)) {
                Val v; v.s_val = internal_map[key]; return v;
            }
            // If writing, we return a Val that updates internal_map? Hard to do with value semantics.
            // For simple "status['state'] = ..."
            return Val();
        }

        // HACK: To support status["state"] = "START" capturing
        // We need a proxy. But let's cheat.
        // We will make dump() generic or just empty.

        std::string value(const char* key, const char* def) {
            if (internal_map.count(key)) return internal_map[key];
            return def;
        }
    };
}
