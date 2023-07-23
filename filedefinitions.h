#pragma once
/*
* The codes in file formats should have no d3d9 components.
*/

#include "general_headers.h"
#include "log.h"

enum class file_type : uint32_t
{
	shp = 0,
	vxl = 1,
	tmp = 2,
	pal = 3,
	vpl = 4,
	hva = 5,
};

enum class normal_type :byte
{
	tiberian_sun = 2,
	red_alert = 4,
};

struct color
{
	byte r{ 0 }, g{ 0 }, b{ 0 };
};

struct vxlmatrix
{
	union {
		struct {
			float        _11, _12, _13, _14;
			float        _21, _22, _23, _24;
			float        _31, _32, _33, _34;
		};
		float _data[3][4];
	};
};

class game_file
{
public:
	game_file() = default;
	virtual ~game_file() = default;
	virtual bool load(const std::string& filename) = 0;
	virtual bool load(const void* data) = 0;
	virtual bool is_loaded() const = 0;
	virtual void purge() = 0;
	virtual file_type type() const = 0;
};

std::shared_ptr<char> read_whole_file(const std::string& filename);
/*
{
	if (!std::filesystem::exists(filename))
	{
		logger::writelog("File does not exist.");
		return nullptr;
	}

	const size_t filesize = std::filesystem::file_size(filename);
	std::fstream file(filename, std::ios::binary);

	if (!file || !filesize)
	{
		return nullptr;
	}

	std::shared_ptr<char> data(new char[filesize]);
	if (!data)
	{
		return nullptr;
	}

	file.read(data.get(), filesize);
	return data;
}*/