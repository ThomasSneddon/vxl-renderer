#include "gdi.h"
#include "hva.h"
#include "vxl.h"
#include "vpl.h"

extern DirectX::XMVECTOR game_normals[256];
coords vxl_projection(const size_t canvas_width, const size_t canvas_height, const coords& position)
{
	double w = canvas_width;
	double h = canvas_height;
	double f = 5000.0f;

	coords result;

	result.x = w / 2.0 + (position.x - position.y) / sqrt(2.0);
	result.y = h / 2.0 + (position.x + position.y) / 2.0 / sqrt(2.0) - position.z * sqrt(3.0) / 2.0;
	result.z = sqrt(3.0) / 2.0 / f * (4000.0 * sqrt(2.0) / 3.0 - (position.x + position.y) / sqrt(2.0) - position.z / sqrt(3.0));

	return result;
}

void deleters::bitmap_deleter(HBITMAP* pbitmap)
{
	if (pbitmap && *pbitmap)
	{
		DeleteObject(*pbitmap);
		*pbitmap = NULL;
	}
}

void deleters::dc_deleter(HDC* hdc)
{
	if (hdc && *hdc)
	{
		DeleteDC(*hdc);
		*hdc = NULL;
	}
}

safe_bitmap::safe_bitmap(const HBITMAP bmp)
{
	reset(bmp);
}

safe_bitmap::safe_bitmap(const safe_bitmap& rhs)
{
	release();
	if (rhs._bitmap)
	{
		_bitmap = rhs._bitmap;
		_ref = rhs._ref;
		++*_ref;
	}
}

safe_bitmap::~safe_bitmap()
{
	reset();
}

void safe_bitmap::reset(const HBITMAP bmp)
{
	//deleters::bitmap_deleter(&_bitmap);

	release();
	if (bmp)
	{
		_ref = new int(1);
		_bitmap = bmp;
	}
}

void safe_bitmap::release()
{
	if (_bitmap != NULL)
	{
		--* _ref;
		if (*_ref == 0)
		{
			delete _ref;
			DeleteObject(_bitmap);
			_bitmap = NULL;
		}
	}
}

safe_bitmap::operator HBITMAP() const
{
	return _bitmap;
}

HBITMAP safe_bitmap::get() const
{
	return (HBITMAP)(*this);
}

safe_bitmap::operator bool() const
{
	return _bitmap != NULL;
}

safe_dc::safe_dc(const HDC hdc)
{
	reset(hdc);
}

safe_dc::safe_dc(safe_dc& rhs)
{
	release();
	if (rhs._dc)
	{
		_dc = rhs._dc;
		_ref = rhs._ref;
		++* _ref;
	}
}

safe_dc::~safe_dc()
{
	reset();
}

void safe_dc::reset(const HDC hdc)
{
	release();
	if (hdc)
	{
		_ref = new int(1);
		_dc = hdc;
	}
}

void safe_dc::release()
{
	if (_dc != NULL)
	{
		--* _ref;
		if (*_ref == 0)
		{
			delete _ref;
			DeleteDC(_dc);
			_dc = NULL;
		}
	}
}

safe_dc::operator HDC()const
{
	return _dc;
}

HDC safe_dc::get() const
{
	return (HDC)(*this);
}

safe_dc::operator bool() const
{
	return _dc != NULL;
}

bitmap_guard::bitmap_guard(HBITMAP* pbitmap) :
	_manager(pbitmap, deleters::bitmap_deleter)
{}

created_dc_guard::created_dc_guard(HDC* hdc) :
	_manager(hdc, deleters::dc_deleter)
{}

bool vxl_gdi_renderer::initialize(HWND hwnd_for_dc)
{
	if(!hwnd_for_dc)
		return false;
	
	HDC window_dc = GetDC(hwnd_for_dc);
	if (!window_dc)
		return false;

	std::unique_ptr<double[][bitmap_width]> zbuffer(new double[bitmap_height][bitmap_width]);
	if (!zbuffer)
		return false;

	HDC own_dc = CreateCompatibleDC(window_dc);
	if (!own_dc)
		return false;

	auto bmibuffer = std::unique_ptr<byte>(new byte[sizeof BITMAPINFO + 256u * sizeof RGBQUAD]);
	auto& bmi = *reinterpret_cast<BITMAPINFO*>(bmibuffer.get());
	bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biWidth = bitmap_width;
	bmi.bmiHeader.biHeight = -(int)bitmap_height;
	bmi.bmiHeader.biBitCount = bitmap_bpp;
	bmi.bmiHeader.biSizeImage = bitmap_pitch * bmi.bmiHeader.biHeight;

	void* color_buffer = nullptr;
	HBITMAP bitmap = CreateDIBSection(own_dc, &bmi, DIB_RGB_COLORS, &color_buffer, NULL, 0);
	if (!bitmap || !color_buffer)
	{
		DeleteDC(own_dc);
		return false;
	}

	_canvas.reset(bitmap);
	_hdc.reset(own_dc);
	_surface_buffer = color_buffer;
	_zbuffer = std::move(zbuffer);

	clear_vxl_canvas({ 0,0,0,0 });
	SelectObject(_hdc.get(), _canvas.get());
	return true;
}

inline bool vxl_gdi_renderer::valid() const
{
	return !!_canvas && !!_hdc && _surface_buffer && _zbuffer;
}

bool vxl_gdi_renderer::clear_vxl_canvas(const RGBQUAD& fill_color)
{
	if (!valid())
		return false;

	RGBQUAD(*colors)[bitmap_width] = reinterpret_cast<decltype(colors)>(_surface_buffer);
	for (size_t y = 0; y < bitmap_height; y++)
		for (size_t x = 0; x < bitmap_width; x++)
		{
			colors[y][x] = fill_color;
			_zbuffer[y][x] = std::numeric_limits<std::remove_reference_t<decltype(_zbuffer[y][x])>>::max();
		}

	return true;
}

bool vxl_gdi_renderer::render_vxl(const vxl& vxl, const hva& hva, const palette& palette, const vpl& vpl, const size_t frame)
{
	if (!valid() || !vxl.is_loaded() || !hva.is_loaded() || !palette.is_loaded() || vxl.limb_count() != hva.section_count())
		return false;

	const color& clear_color = palette.entry()[0];
	clear_vxl_canvas({ clear_color.b,clear_color.g,clear_color.r,255u });
	const size_t drawing_frame = frame >= hva.frame_count() ? 0 : frame;
	for (size_t section_idx = 0; section_idx < hva.section_count(); section_idx++)
	{
		const vxlmatrix matrix = *hva.matrix(drawing_frame, section_idx);
		const vxl_limb_tailer& tailer = *vxl.limb_tailer(section_idx);
		DirectX::XMMATRIX translation_to_center = {}, scale = {}, base = {}, position_transform = {}, normal_transform = {};
		DirectX::XMVECTOR scale_vec = {}, max_bound = {}, min_bound = {}, vxl_dimension = {};

		vxl_dimension = { (float)tailer.xsize,(float)tailer.ysize,(float)tailer.zsize };
		max_bound.vector4_f32[0] = tailer.max_bounds[0];
		max_bound.vector4_f32[1] = tailer.max_bounds[1];
		max_bound.vector4_f32[2] = tailer.max_bounds[2];
		min_bound.vector4_f32[0] = tailer.min_bounds[0];
		min_bound.vector4_f32[1] = tailer.min_bounds[1];
		min_bound.vector4_f32[2] = tailer.min_bounds[2];
		scale_vec = (max_bound - min_bound) / vxl_dimension;
		base.m[0][0] = matrix._data[0][0];
		base.m[0][1] = matrix._data[1][0];
		base.m[0][2] = matrix._data[2][0];
		base.m[1][0] = matrix._data[0][1];
		base.m[1][1] = matrix._data[1][1];
		base.m[1][2] = matrix._data[2][1];
		base.m[2][0] = matrix._data[0][2];
		base.m[2][1] = matrix._data[1][2];
		base.m[2][2] = matrix._data[2][2];
		base.m[3][0] = matrix._data[0][3] * scale_vec.vector4_f32[0] * tailer.scale;
		base.m[3][1] = matrix._data[1][3] * scale_vec.vector4_f32[1] * tailer.scale;
		base.m[3][2] = matrix._data[2][3] * scale_vec.vector4_f32[2] * tailer.scale;
		translation_to_center = DirectX::XMMatrixTranslationFromVector(min_bound);
		scale = DirectX::XMMatrixScalingFromVector(scale_vec);
		position_transform = translation_to_center * scale * base * _states.world;
		normal_transform = base * _states.world;
		DirectX::XMVECTOR uz = { 0.0f,0.0f,1.0f,0.0f }, uxyz = { 1.0f,1.0f,1.0f,0.0f },
			ux = { 1.0f,0.0f,0.0f,0.0f }, uy = { 0.0f,1.0f,0.0f,0.0f },
			base_coords = { 0.0f,0.0f,0.0f,1.0f }, transformed_base = {},
			transformed_x = {}, transformed_y = {}, transformed_z = {};

		transformed_base = DirectX::XMVector4Transform(base_coords, position_transform);
		transformed_x = DirectX::XMVector4Transform(ux, position_transform);
		transformed_y = DirectX::XMVector4Transform(uy, position_transform);
		transformed_z = DirectX::XMVector4Transform(uz, position_transform);

		RGBQUAD(*surface_buffer)[bitmap_width] = reinterpret_cast<decltype(surface_buffer)>(_surface_buffer);
		const auto l = _states.light;
		auto l2 = l + uz;
		if (DirectX::XMVector4Length(l2).vector4_f32[0] == 0.0f)
			l2 = DirectX::XMVector4Length(l2);
		else
			l2 /= DirectX::XMVector4Length(l2);

		static const DirectX::XMVECTOR camera_dir = { 1.0f,1.0f,sqrt(2.0f) / sqrt(3.0f),0.0f };
		static size_t vpl_reindex_table[_countof(game_normals)] = {0};
		static const double alpha = 3.0;

		for (size_t i = 0; i < _countof(vpl_reindex_table); i++)
		{
			const auto& normal = game_normals[i];
			auto transformed_normal = DirectX::XMVector4Transform(normal, normal_transform); 
			double cos_n_l = DirectX::XMVector4Dot(transformed_normal, l).vector4_f32[0];
			double cos_n_l2 = DirectX::XMVector4Dot(transformed_normal, l2).vector4_f32[0];
			double f = std::max(0.0, cos_n_l);
			double f2 = std::max(0.0, cos_n_l2 / (alpha - (alpha - 1.0) * cos_n_l2));
			size_t vpl_table_idx = static_cast<size_t>(16.0 * (f + f2));
			vpl_reindex_table[i] = vpl_table_idx;
		}

		for (size_t z = 0; z < tailer.zsize; z++)
		{
			for (size_t y = 0; y < tailer.ysize; y++)
			{
				for (size_t x = 0; x < tailer.xsize; x++)
				{
					const voxel& vox = vxl.voxel_rh(section_idx, x, y, z);
					if (!vox.color)
						continue;

					auto normal = game_normals[vox.normal];
					auto transformed_normal = DirectX::XMVector4Transform(normal, normal_transform);
					if (DirectX::XMVector4Dot(transformed_normal, camera_dir).vector4_f32[0] < 0.0f)
						continue;

					DirectX::XMVECTOR vox_pos = { static_cast<float>(x),static_cast<float>(y),static_cast<float>(z),0.0f };
					DirectX::XMVECTOR model_pos = transformed_base + x * transformed_x + y * transformed_y + z * transformed_z;
					coords pos = { model_pos.vector4_f32[0],model_pos.vector4_f32[1],model_pos.vector4_f32[2] };
					coords screen_pos = vxl_projection(bitmap_width, bitmap_height, pos);
					const size_t bufferx = static_cast<size_t>(screen_pos.x);
					const size_t buffery = static_cast<size_t>(screen_pos.y);

					if (screen_pos.x >= bitmap_width || screen_pos.x < 0 || screen_pos.y >= bitmap_height || screen_pos.y < 0)
						continue;

					if (screen_pos.z >= _zbuffer[buffery][bufferx])
						continue;

					size_t vpl_table_idx = vpl_reindex_table[vox.normal];
					const color& real_color = palette.entry()[vpl.data()[vpl_table_idx][vox.color]];
					RGBQUAD& writing_color = surface_buffer[buffery][bufferx];
					writing_color.rgbBlue = real_color.b;
					writing_color.rgbGreen = real_color.g;
					writing_color.rgbRed = real_color.r;
				}
			}
		}
	}

	return true;
}

bool vxl_gdi_renderer::copy_result(HDC target)
{
	if (!valid())
		return false;

	return BitBlt(target, 0, 0, bitmap_width, bitmap_height, _hdc.get(), 0, 0, SRCCOPY);
}

void vxl_gdi_renderer::set_world(const DirectX::XMMATRIX& world)
{
	_states.world = world;
}

void vxl_gdi_renderer::set_light_dir(const DirectX::XMVECTOR& dir)
{
	_states.light = dir;
}

DirectX::XMMATRIX vxl_gdi_renderer::get_world() const
{
	return _states.world;
}

DirectX::XMVECTOR vxl_gdi_renderer::get_light_dir() const
{
	return _states.light;
}
