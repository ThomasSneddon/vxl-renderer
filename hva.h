#pragma once

#include "filedefinitions.h"

class hva : public game_file
{
public:
	hva() = default;
	virtual ~hva() = default;

	hva(const std::string& filename);

	virtual bool load(const std::string& filename) final;
	virtual bool load(const void* data) final;
	virtual bool is_loaded() const final;
	virtual void purge() final;
	virtual file_type type() const final;

	const size_t frame_count()const;
	const size_t section_count()const;
	const vxlmatrix* matrix(const size_t frame, const size_t section) const;

private:
	char _signature[0x10u]{ 0 };
	uint32_t _framecount{ 0 };
	uint32_t _sectioncount{ 0 };
	std::vector<vxlmatrix> _totalmatrices;
};