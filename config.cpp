#include "config.h"

#include <filesystem>

config::config(const std::string& filename)
{
    load(filename);
}

bool config::load(const std::string& filename)
{
    //char linebuff[0x200]{ 0 };
    std::ifstream file(filename);

    if (!file)
        return false;

    std::string current_section, filestring;
    size_t filesize = std::filesystem::file_size(filename);
    filestring.resize(filesize);
    file.read(filestring.data(), filesize);
    *(filestring.end() - 1) = 0;
    size_t off = 0;
    std::string key;
    while (getline(filestring, off, key))
    {
        if (is_annotation(key) || is_empty(key))
            continue;

        remove_annotation(key);

        if (is_section(key))
        {
            current_section = key;
            convert_section_name(current_section);
            _config[current_section] = section_type();
            continue;
        }

        std::string right = split(key);
        value_type values = split_values(right);

        if (values.empty() || key.empty())
            continue;

        _config[current_section][key] = values;
    }

    return is_loaded();
}

bool config::is_loaded()
{
    return !_config.empty();
}

void config::clear()
{
    _config.clear();
}

config::section_type config::operator[](const std::string& name)
{
    return section(name);
}

config::section_type config::section(const std::string& name)
{
    section_type section;

    config_type::const_iterator iter = _config.find(name);
    if (iter == _config.end())
        return section;
    return iter->second;
}

config::value_type config::value(const std::string& secname, const std::string& key)
{
    value_type values;
    section_type sec = section(secname);

    if (sec.empty())
        return values;

    section_type::const_iterator iter = sec.find(key);
    if (iter == sec.end())
        return values;
    return iter->second;
}

std::vector<int> config::value_as_int(const std::string& section, const std::string& key)
{
    std::vector<int> ret;
    value_type values = value(section, key);

    ret.resize(values.size());
    for (size_t i = 0; i < ret.size(); i++)
    {
        ret[i] = atoi(values[i].c_str());
    }

    return ret;
}

std::vector<bool> config::value_as_bool(const std::string& section, const std::string& key, bool def)
{
    std::vector<bool> ret;
    value_type values = value(section, key);

    for (std::string value : values)
    {
        char first = std::toupper(value.front());
        if (first == 'T' || first == 'Y' || first == '1')
            ret.push_back(true);
        else if (first == 'F' || first == 'N' || first == '0')
            ret.push_back(false);
        else
            ret.push_back(def);
    }

    return ret;
}

std::vector<std::string> config::value_as_strings(const std::string& section, const std::string& key)
{
    return value(section, key);
}

int config::read_int(const std::string& section, const std::string& key, int def)
{
    std::vector<int> values = value_as_int(section, key);
    if (values.empty())
        return def;
    return values.front();
}

bool config::read_bool(const std::string& section, const std::string& key, bool def)
{
    std::vector<bool> values = value_as_bool(section, key, def);
    if (values.empty())
        return def;
    return values.front();
}

std::string config::read_string(const std::string& section, const std::string& key, std::string def)
{
    value_type values = value(section, key);
    if (values.empty() || values.front().empty())
        return def;
    return values.front();
}

void config::trim(std::string& string, const char* filter)
{
    string.erase(0, string.find_first_not_of(filter));
    string.erase(string.find_last_not_of(filter) + 1);//std::string::npos + 1 == 0;
    string.erase(string.find_last_not_of('\0') + 1);//std::string::npos + 1 == 0;
}

void config::remove_annotation(std::string& string)
{
    if (string.find(';') != string.npos)
        string.erase(string.find(';'));

    if (string.find("//") != string.npos)
        string.erase(string.find("//"));
}

std::string config::split(std::string& string, char delim)
{
    std::string right;

    if (string.find(delim) != string.npos)
    {
        right = string;
        string.erase(string.find(delim));
        trim(string);

        right.erase(0, right.find(delim) + 1);
        trim(right);
    }

    return right;
}

config::value_type config::split_values(const std::string& string)
{
    std::vector<std::string> values;
    std::string current;
    size_t current_off;
    size_t string_off;

    string_off = 0;
    current_off = string.find(',');
    current.assign(string, string_off, current_off);
    trim(current);

    while (!current.empty())
    {
        values.push_back(current);
        if (current_off == std::string::npos)
            break;

        string_off = current_off + 1;
        current_off = string.find(',', current_off + 1);
        current.assign(string, string_off, current_off - string_off);
        trim(current);
    }

    return values;
}

bool config::is_section(const std::string& string)
{
    std::string copy = string;

    trim(copy);
    return !copy.empty() && copy.front() == '[' && copy.back() == ']' && copy.find(';') == std::string::npos;
}

bool config::is_empty(const std::string string)
{
    std::string copy = string;

    trim(copy);
    return copy.empty();
}

void config::convert_section_name(std::string& string)
{
    trim(string, " \t\r\n;[]");
}

bool config::is_annotation(const std::string& string)
{
    std::string copy = string;
    trim(copy, " \t\r\n");

    return !copy.empty() && (copy.front() == ';' || copy.substr(0, 2) == "//");
}
bool config::getline(const std::string& filestring, size_t& off, std::string& result)
{
    if (filestring.length() <= off)
        return false;

    if (size_t retpos = filestring.find('\n', off); retpos != filestring.npos) {
        result.assign(filestring.begin() + off, filestring.begin() + retpos);
        off = retpos + 1;

        return true;
    }
    else
    {
        result.assign(filestring.begin() + off, filestring.end());
        off = filestring.npos;
        return !result.empty();
    }

    return false;
}
