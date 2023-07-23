#pragma once

#include "filedefinitions.h"
#include "pal.h"

#pragma pack(1)
struct vxl_header
{
	char signature[0x10]{ 0 };
	uint32_t _reserved{ 0 };
	uint32_t limb_count{ 0 };
private:
	uint32_t _limb_count{ 0 };
public:
	uint32_t body_size{ 0 };
	uint8_t remap_start{ 0xffu };
	uint8_t remap_end{ 0xffu };
	color internal_palette[0x100]{ 0 };
};
#pragma pack()

struct voxel
{
	byte color{ 0 };
	byte normal{ 0 };
};

struct vxl_limb_header
{
	char name[16];
	int32_t limb_number;
	uint32_t _reserved1;
	uint32_t _reserved2;
};

struct vxl_limb_tailer
{
	uint32_t span_start_offset;
	uint32_t span_end_offset;
	uint32_t span_data_offset;
	float_t scale;
	vxlmatrix matrix;
	float min_bounds[3];//xyz
	float max_bounds[3];//xyz
	uint8_t xsize;
	uint8_t ysize;
	uint8_t zsize;
	normal_type normal_type;
};

struct span_data
{
	uint8_t voxels_size = 0;
	std::vector<voxel> voxels;
};

struct vxl_limb
{
	std::vector<uint32_t> span_starts;
	std::vector<uint32_t> span_ends;
	std::vector<span_data> span_data_blocks;
};

class vxl : public game_file
{
public:
	vxl() = default;
	virtual ~vxl() = default;

	vxl(const std::string& filename);

	virtual bool load(const std::string& filename) final;
	virtual bool load(const void* data) final;
	virtual bool is_loaded() const final;
	virtual void purge() final;
	virtual file_type type() const final;

	size_t limb_count()const;
	const vxl_limb_tailer* limb_tailer(const size_t limb) const;
	const vxl_limb_header* limb_header(const size_t limb) const;
	voxel voxel_lh(const size_t limb, const uint32_t x, const uint32_t y, const uint32_t z) const;
	voxel voxel_rh(const size_t limb, const uint32_t x, const uint32_t y, const uint32_t z) const;

private:
	vxl_header _fileheader;
	std::vector<vxl_limb> _body_data;
	std::vector<vxl_limb_header> _headers;
	std::vector<vxl_limb_tailer> _tailers;
};