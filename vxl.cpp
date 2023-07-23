#include "vxl.h"

vxl::vxl(const std::string& filename) :vxl()
{
	load(filename);
}

bool vxl::load(const std::string& filename)
{
	auto data = read_whole_file(filename);
	return load(data.get());
}

bool vxl::load(const void* data)
{
	if (!data)
	{
		return false;
	}

	purge();

	byte* floating_cur = reinterpret_cast<byte*>(const_cast<void*>(data));

	memcpy(&_fileheader, floating_cur, sizeof _fileheader);
	for (color& color : _fileheader.internal_palette)
	{
		color.r <<= 2;
		color.g <<= 2;
		color.b <<= 2;
	}

	size_t limb_count = _fileheader.limb_count;
	_body_data.resize(limb_count);
	_headers.resize(limb_count);
	_tailers.resize(limb_count);

	floating_cur += sizeof _fileheader;
	memcpy(_headers.data(), floating_cur, limb_count * sizeof vxl_limb_header);

	floating_cur += limb_count * sizeof vxl_limb_header + _fileheader.body_size;
	memcpy(_tailers.data(), floating_cur, limb_count * sizeof vxl_limb_tailer);

	floating_cur -= _fileheader.body_size;
	for (size_t i = 0; i < limb_count; i++)
	{
		vxl_limb_header& current_header = _headers[i];
		vxl_limb_tailer& current_tailer = _tailers[i];
		vxl_limb& current_body = _body_data[i];

		size_t span_count = current_tailer.xsize * current_tailer.ysize;
		current_body.span_data_blocks.resize(span_count);
		current_body.span_ends.resize(span_count);
		current_body.span_starts.resize(span_count);

		byte* span_data_blocks = floating_cur + current_tailer.span_data_offset;
		byte* span_start_data = floating_cur + current_tailer.span_start_offset;
		byte* span_end_data = floating_cur + current_tailer.span_end_offset;

		memcpy(current_body.span_starts.data(), span_start_data, span_count * sizeof uint32_t);
		memcpy(current_body.span_ends.data(), span_end_data, span_count * sizeof uint32_t);

		for (size_t n = 0; n < span_count; n++)
		{
			span_data& current_span = current_body.span_data_blocks[n];
			current_span.voxels_size = 0;
			current_span.voxels.resize(current_tailer.zsize);

			if (current_body.span_starts[n] == 0xffffffffu || current_body.span_ends[n] == 0xffffffffu)
			{
				continue;
			}

			byte* current_span_data = span_data_blocks + current_body.span_starts[n];
			byte* current_span_end = span_data_blocks + current_body.span_ends[n];

			size_t current_vox_idx = 0;
			byte skip_count, voxel_count, voxel_end;

			do
			{
				skip_count = *current_span_data++;
				voxel_count = *current_span_data++;
				current_vox_idx += skip_count;

				if (voxel_count)
				{
					memcpy(&current_span.voxels[current_vox_idx], current_span_data, voxel_count * sizeof voxel);
					current_span_data += voxel_count * sizeof voxel;
				}

				current_vox_idx += voxel_count;
				voxel_end = *current_span_data++;

				if (voxel_count != voxel_end);
				//error report here

			} while (current_span_data <= current_span_end);

			for (size_t z = 0; z < current_tailer.zsize; z++)
			{
				if (current_span.voxels[z].color)
					current_span.voxels_size++;
			}
		}
	}

	return true;
}

bool vxl::is_loaded() const
{
	return !_tailers.empty();
}

void vxl::purge()
{
	_fileheader = vxl_header();
	_body_data.clear();
	_headers.clear();
	_tailers.clear();
}

file_type vxl::type() const
{
	return file_type::vxl;
}

size_t vxl::limb_count() const
{
	return _fileheader.limb_count;
}

const vxl_limb_tailer* vxl::limb_tailer(const size_t limb) const
{
	if (!is_loaded() || limb >= limb_count())
		return nullptr;
	return &_tailers[limb];
}

const vxl_limb_header* vxl::limb_header(const size_t limb) const
{
	if (!is_loaded() || limb >= limb_count())
		return nullptr;
	return &_headers[limb];
}

voxel vxl::voxel_lh(const size_t limb, const uint32_t x, const uint32_t y, const uint32_t z) const
{
	return voxel_rh(limb, y, x, z);
}

voxel vxl::voxel_rh(const size_t limb, const uint32_t x, const uint32_t y, const uint32_t z) const
{
	voxel result;
	if (!is_loaded() || limb >= limb_count())
		return result;

	const vxl_limb_tailer& tailer = _tailers[limb];
	const vxl_limb& body = _body_data[limb];

	if (x >= tailer.xsize || y >= tailer.ysize || z >= tailer.zsize)
		return result;

	return body.span_data_blocks[y * tailer.xsize + x].voxels[z];
}