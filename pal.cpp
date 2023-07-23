#include "pal.h"

palette::palette(const std::string& filename) :palette()
{
	load(filename);
}

bool palette::load(const std::string& filename)
{
	const size_t filesize = std::filesystem::file_size(filename);
	if (filesize != sizeof _entries)
	{
		return false;
	}

	auto data = read_whole_file(filename);
	return load(data.get());
}

bool palette::load(const void* data)
{
	if (!data)
	{
		return false;
	}

	memcpy_s(_entries, sizeof _entries, data, sizeof _entries);
	for (auto& color : _entries)
	{
		color.r <<= 2;
		color.g <<= 2;
		color.b <<= 2;
	}

	return true;
}

bool palette::is_loaded() const
{
	return true;
}

void palette::purge()
{
	memset(_entries, 0, sizeof _entries);
}

file_type palette::type() const
{
	return file_type::pal;
}

const color* palette::entry() const
{
	return _entries;
}


