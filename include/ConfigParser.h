#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <iostream>
#include <fstream>
#include <string>
#include <map>

class ConfigParser {
private:
    std::map<std::string, std::map<std::string, std::string>> data_;

    std::string trim(const std::string& str) {
        std::string s = str;
        size_t pos;
        while ((pos = s.find("\xC2\xA0")) != std::string::npos) {
            s.replace(pos, 2, " ");
        }
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, (last - first + 1));
    }

public:
    ConfigParser(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "\033[1;31m[ConfigParser Error] Cannot open file: \033[0m" << filename << "\n";
            return;
        }

        std::string line, current_section;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line.front() == '[' && line.back() == ']') {
                current_section = trim(line.substr(1, line.size() - 2));
            } else {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = trim(line.substr(0, eq_pos));
                    std::string val = trim(line.substr(eq_pos + 1));
                    data_[current_section][key] = val;
                }
            }
        }
    }

    int GetInt(const std::string& section, const std::string& key, int default_val) {
        if (data_.count(section) && data_[section].count(key)) {
            try { return std::stoi(data_[section][key]); }
            catch (...) { return default_val; }
        }
        return default_val;
    }
};

#endif // CONFIG_PARSER_H