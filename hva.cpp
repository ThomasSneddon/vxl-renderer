#include "hva.h"

hva::hva(const std::string& filename) :hva()
{
	load(filename);
}

bool hva::load(const std::string& filename)
{
	auto data = read_whole_file(filename);
	return load(data.get());
}

bool hva::load(const void* data)
{
	if (!data)
	{
		return false;
	}

	char* filedata = reinterpret_cast<char*>(const_cast<void*>(data));
	char* filecur = filedata;

	purge();

	memcpy_s(_signature, sizeof _signature, filecur, sizeof _signature);
	filecur += sizeof _signature;
	_framecount = *reinterpret_cast<uint32_t*>(filecur);
	filecur += sizeof _framecount;
	_sectioncount = *reinterpret_cast<uint32_t*>(filecur);
	filecur += sizeof _sectioncount + /*section names*/section_count() * sizeof _signature;

	size_t total_maxticx_count = section_count() * frame_count();
	_totalmatrices.resize(total_maxticx_count);
	memcpy_s(_totalmatrices.data(), total_maxticx_count * sizeof vxlmatrix, filecur, total_maxticx_count * sizeof vxlmatrix);

	return true;
}

bool hva::is_loaded() const
{
	return !_totalmatrices.empty();
}

void hva::purge()
{
	_signature[0] = 0;
	_framecount = _sectioncount = 0;
	_totalmatrices.clear();
}

file_type hva::type() const
{
	return file_type::hva;
}

const size_t hva::frame_count() const
{
	return _framecount;
}

const size_t hva::section_count() const
{
	return _sectioncount;
}

const vxlmatrix* hva::matrix(const size_t frame, const size_t section) const
{
	return (is_loaded() && frame < frame_count() && section < section_count()) ?
		&_totalmatrices[frame * section_count() + section] : nullptr;
}
