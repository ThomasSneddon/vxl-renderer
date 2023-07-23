#pragma once

#include "general_headers.h"

namespace deleters
{
	void bitmap_deleter(HBITMAP* pbitmap);
	void dc_deleter(HDC* hdc);

	using pbmp_deleter_type = decltype(bitmap_deleter)*;
	using pdc_deleter_type = decltype(dc_deleter)*;
}

struct coords
{
	double x{ 0 }, y{ 0 }, z{ 0 };
};

struct point
{
	double x{ 0 }, y{ 0 };
};

class safe_bitmap
{
public:
	safe_bitmap(const HBITMAP bmp);
	safe_bitmap() = default;
	safe_bitmap(const safe_bitmap& rhs);
	~safe_bitmap();
	void reset(const HBITMAP bmp = NULL);
	void release();
	operator HBITMAP()const;
	HBITMAP get()const;
	operator bool() const;
private:
	HBITMAP _bitmap{ NULL };
	int* _ref{ nullptr };
};

//cannot be used with GetDC
class safe_dc
{
public:
	safe_dc(const HDC hdc);
	safe_dc() = default;
	safe_dc(safe_dc& rhs);
	~safe_dc();
	void reset(const HDC hdc = NULL);
	void release();
	operator HDC()const;
	HDC get()const;
	operator bool()const;
private:
	HDC _dc{ NULL };
	int* _ref{ nullptr };
};

class bitmap_guard
{
public:
	bitmap_guard(HBITMAP* pbitmap);
private:
	std::unique_ptr<HBITMAP, deleters::pbmp_deleter_type> _manager;
};

class created_dc_guard
{
public:
	created_dc_guard(HDC* hdc);
private:
	std::unique_ptr<HDC, deleters::pdc_deleter_type> _manager;
};

struct scene_states
{
	DirectX::XMMATRIX world{ DirectX::XMMatrixIdentity() };
	DirectX::XMVECTOR light{ 0.2013022f,0.9101138f,-0.3621709f,0.0f };
};
class vxl_gdi_renderer
{
public:
	constexpr static const size_t bitmap_width = 256;
	constexpr static const size_t bitmap_height = 256;
	constexpr static const size_t bitmap_bpp = 32;
	constexpr static const size_t bitmap_pitch = ((bitmap_width * bitmap_bpp + 31) & ~31) / 8;

	vxl_gdi_renderer() = default;
	~vxl_gdi_renderer() = default;

	bool initialize(HWND hwnd_for_dc);
	bool valid()const;
	bool clear_vxl_canvas(const RGBQUAD& fill_color);
	bool render_vxl(const class vxl& vxl, const class hva& hva, const class palette& palette, const class vpl& vpl,const size_t frame);
	bool copy_result(HDC target);
	void set_world(const DirectX::XMMATRIX& world);
	void set_light_dir(const DirectX::XMVECTOR& dir);
	DirectX::XMMATRIX get_world()const;
	DirectX::XMVECTOR get_light_dir()const;

private:
	safe_bitmap _canvas;
	safe_dc _hdc;
	void* _surface_buffer{ 0 };
	std::unique_ptr<double[][bitmap_width]> _zbuffer;
	scene_states _states;
};