#pragma once

#include "filedefinitions.h"

class config
{
public:
	using value_type = std::vector<std::string>;//a value consists of multiple splited values
	using section_type = std::unordered_map<std::string, value_type>;//a section contains multiple key-value pairs
	using config_type = std::unordered_map<std::string, section_type>;//a config contains multiple sections

	config() = default;
	~config() = default;
	config(const std::string& filename);

	//
	bool load(const std::string& filename);
	bool is_loaded();
	void clear();

	//
	section_type operator[](const std::string& section);
	section_type section(const std::string& name);
	value_type value(const std::string& section, const std::string& key);

	std::vector<int> value_as_int(const std::string& section, const std::string& key);
	std::vector<bool> value_as_bool(const std::string& section, const std::string& key, bool def);
	std::vector<std::string> value_as_strings(const std::string& section, const std::string& key);

	int read_int(const std::string& section, const std::string& key, int def);
	bool read_bool(const std::string& section, const std::string& key, bool def);
	std::string read_string(const std::string& section, const std::string& key, std::string def);

	//private:
	static void trim(std::string& string, const char* filter = " \t\r\n");
	static void remove_annotation(std::string& string);
	static std::string split(std::string& left, char delim = '=');
	static value_type split_values(const std::string& string);
	static bool is_section(const std::string& string);
	static bool is_empty(const std::string string);
	static void convert_section_name(std::string& string);
	static bool is_annotation(const std::string& string);
	static bool getline(const std::string& filestring, size_t& off, std::string& result);

	config_type _config;
};

