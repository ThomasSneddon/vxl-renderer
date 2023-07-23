#include "filedefinitions.h"

std::shared_ptr<char> read_whole_file(const std::string& filename)
{
	if (!std::filesystem::exists(filename))
	{
		LOG(ERROR) << "File " << filename.c_str() << " does not exist.\n";
		return nullptr;
	}

	const size_t filesize = std::filesystem::file_size(filename);
	std::ifstream file(filename, std::ios::binary);

	if (!file.is_open() || !filesize)
	{
		LOG(ERROR) << "File " << filename.c_str() << " is invalid.\n";
		LOG(ERROR) << "Size = " << filesize << ".\n";
		return nullptr;
	}

	std::shared_ptr<char> data(new char[filesize]);
	if (!data)
	{
		LOG(ERROR) << "Failed to allocate file buffer for " << filename.c_str() << ".\n";
		return nullptr;
	}

	file.read(data.get(), filesize);
	return data;
}