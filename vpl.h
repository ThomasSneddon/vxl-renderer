#pragma once

#include "filedefinitions.h"
#include "pal.h"

struct vplheader
{
	uint32_t remap_start{ 0xffffffffu };
	uint32_t remap_end{ 0xffffffffu };
	uint32_t section_count{ 0 };
	uint32_t _reserved{ 0 };
};

class vpl : public game_file
{

public:
	vpl() = default;
	virtual ~vpl() = default;

	vpl(const std::string& filename);

	virtual bool load(const std::string& filename) final;
	virtual bool load(const void* data) final;
	virtual bool is_loaded() const final;
	virtual void purge() final;
	virtual file_type type() const final;

	byte(*data() const)[256];

private:
	vplheader _header;
	palette _internal_pal;
	std::shared_ptr<byte[][256]> _sections;
};