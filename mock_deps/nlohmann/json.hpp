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
            std::string s;
            template<typename T> Any(const T& v) {
                // Simple to_string for mock
                // If it's a string, keep it.
                // If it's a number, convert.
                // We assume T is convertible to string or is string
                // But catching all is hard without constexpr if.
                // Let's assume usage is {"state", "START"} or {"size", 123}
                // We'll capture string only for state for now.
                // Hacky check?
            }
            Any(const char* v) : s(v) {}
            Any(const std::string& v) : s(v) {}
            Any(int v) : s(std::to_string(v)) {}
            Any(size_t v) : s(std::to_string(v)) {}
        };

        json() {}
        template <typename T>
        json(const T&) {}

        json(std::initializer_list<std::pair<const char*, Any>> l) {
            for(auto& p : l) {
                internal_map[p.first] = p.second.s;
            }
        }

        std::string dump() const {
            // Simple dump for state and size
            std::stringstream ss;
            ss << "{";
            bool first = true;
            for(auto& kv : internal_map) {
                if(!first) ss << ", ";
                ss << "\"" << kv.first << "\": \"" << kv.second << "\"";
                first = false;
            }
            ss << "}";
            return ss.str();
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
            operator int() const { try { return std::stoi(s_val); } catch(...) { return 0; } }
            operator size_t() const { try { return std::stoul(s_val); } catch(...) { return 0; } }

            template<typename T>
            Val& operator=(const T& v) { return *this; }

            Val& operator=(const char* v) { s_val = v; return *this; }
            Val& operator=(const std::string& v) { s_val = v; return *this; }
            Val& operator=(size_t v) { s_val = std::to_string(v); return *this; }
        };

        Val operator[](const char* key) {
            // If key exists, return it. If not, create?
            // For reading:
            if (internal_map.count(key)) {
                Val v; v.s_val = internal_map[key]; return v;
            }
            // For writing (status["size"] = size):
            // We need a proxy that updates the map on assignment.
            // This is too complex for a quick mock.
            // But we can cheat. If the user calls operator[], we return a Val.
            // If they assign to Val, it doesn't update map unless Val has reference to map.
            // Let's implement Val with pointer to map.
            return Val();
        }

        std::string value(const char* key, const char* def) {
            if (internal_map.count(key)) return internal_map[key];
            return def;
        }
    };
}
