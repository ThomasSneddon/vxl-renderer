#pragma once

#include "filedefinitions.h"

class palette : public game_file
{
public:
	palette() = default;
	virtual ~palette() = default;

	palette(const std::string& filename);

	virtual bool load(const std::string& filename) final;
	virtual bool load(const void* data) final;
	virtual bool is_loaded() const final;
	virtual void purge() final;
	virtual file_type type() const final;

	const color* entry() const;
private:
	color _entries[256]{ 0 };
};