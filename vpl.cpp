#include "vpl.h"

vpl::vpl(const std::string& filename) :vpl()
{
	load(filename);
}

bool vpl::load(const std::string& filename)
{
	const size_t filesize = std::filesystem::file_size(filename);
	if (filesize != sizeof _header + sizeof(color[256]) + (32 * sizeof _sections[0]))
	{
		return false;
	}

	auto data = read_whole_file(filename);
	return load(data.get());
}

bool vpl::load(const void* data)
{
	if (!data)
	{
		return false;
	}

	char* filedata = reinterpret_cast<char*>(const_cast<void*>(data));
	char* filecur = filedata;

	purge();

	memcpy_s(&_header, sizeof _header, filecur, sizeof _header);
	filecur += sizeof _header;
	const size_t table_size = _header.section_count * 256;

	_internal_pal.load(filecur);
	filecur += sizeof(color[256]);

	_sections.reset(new byte[_header.section_count][256]);
	if (!_sections)
	{
		purge();
		return false;
	}

	memcpy_s(_sections.get(), table_size, filecur, table_size);
	return true;
}

bool vpl::is_loaded() const
{
	return !!_sections;
}

void vpl::purge()
{
	_header = vplheader();
	_internal_pal.purge();
	_sections.reset();
}

file_type vpl::type() const
{
	return file_type::vpl;
}

bool vpl::save(const std::filesystem::path path)
{
	if (!is_loaded())
		return false;

	std::ofstream output(path.string(), std::ios::binary);

	output.write(reinterpret_cast<const char*>(&_header), sizeof _header);
	output.write(reinterpret_cast<const char*>(_internal_pal.entry()), sizeof(color[256]));
	output.write(reinterpret_cast<const char*>(_sections.get()), _header.section_count * 256);

	output.close();
	return true;
}

byte(*vpl::data() const)[256]
{
	return _sections.get();
}
