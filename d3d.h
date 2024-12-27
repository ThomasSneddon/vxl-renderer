#pragma once

#include "general_headers.h"
#include "com_ptr.hpp"

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>

/*
* 每一个d3d renderer 输出绑定一个窗口
*/ 

#define D3D12_RESOURCE_STATE_SHADER_RESOURCE \
	(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)

enum class blend_op :uint32_t
{
	alpha = 0,//normal
	addictive = 1,
	shadow = 2,//
};

struct canvas_vertex_data
{
	float position[3];
	float uv[2];
};

struct canvas_vertecies_data
{
	canvas_vertecies_data();

	canvas_vertex_data vertex[3];
protected:
	byte _padding[256u - sizeof(vertex)]{ 0 };
};

struct d3d12_render_target_set
{
	com_ptr<ID3D12Device> ref_device;
	std::vector<com_ptr<ID3D12Resource>> targets;
	com_ptr<ID3D12DescriptorHeap> rtv_table;

	void discard();
	bool valid() const;

	~d3d12_render_target_set() = default;
	bool initailize(com_ptr<ID3D12Device> device, const size_t number_of_targets,
		const DXGI_FORMAT format, const size_t width, const size_t height);
};

struct d3d12_swapchain
{
	com_ptr<ID3D12CommandQueue> ref_device;
	com_ptr<IDXGISwapChain3> swapchain;
	std::vector<com_ptr<ID3D12Resource>> targets;
	size_t current_idx = { 0 };
	DXGI_SWAP_CHAIN_DESC desc = {};

	void discard();
	bool valid() const;
	bool initialize(com_ptr<ID3D12CommandQueue> device_prox, com_ptr<IDXGIFactory6> factory, HWND output);
};

struct d3d12_resource_set
{
	//com_ptr<ID3D12Device> ref_device;
	std::vector<com_ptr<ID3D12Resource>> resources;
	//com_ptr<ID3D12DescriptorHeap> desc_table;

	~d3d12_resource_set() = default;
	void discard();
	bool valid() const;

	bool add_empty_resource(com_ptr<ID3D12Device> device,
		const DXGI_FORMAT format,
		const D3D12_RESOURCE_DIMENSION dimension,
		const size_t width, const size_t height, const size_t depth_or_arraysize,
		const D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT,
		const D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);

	//bool bind_resource(const size_t res_index, const size_t bind_index);
	//bool unbind_resource(const size_t bind_index);
};

struct d3d12_command_list
{
	com_ptr<ID3D12Device> ref_device;
	com_ptr<ID3D12GraphicsCommandList> commands;
	com_ptr<ID3D12CommandAllocator> allocator;
	bool started{ false };

	~d3d12_command_list();
	bool initialize(com_ptr<ID3D12Device> device, const D3D12_COMMAND_LIST_TYPE type);
	com_ptr<ID3D12GraphicsCommandList> record();
	bool valid() const;
	bool close();
	bool reset();
	void discard();
};

struct d3d12_fence
{
	com_ptr<ID3D12Device> ref_device;
	com_ptr<ID3D12CommandQueue> ref_queue;
	com_ptr<ID3D12Fence> fence;
	uint64_t fence_value{ 0 };
	HANDLE sync_event{ INVALID_HANDLE_VALUE };

	~d3d12_fence();
	void reset_fence();
	bool initialize(com_ptr<ID3D12Device> device, com_ptr<ID3D12CommandQueue> queue);
	bool valid() const;
	bool raise_fence();
	bool await_completion(const size_t time = 0xffffffffu);
	void discard();
};

struct vxl_cbuffer_data
{
	DirectX::XMVECTOR vxl_dimension{ 0 };
	DirectX::XMVECTOR vxl_minbound{ 0 };
	DirectX::XMVECTOR vxl_maxbound{ 0 };
	DirectX::XMVECTOR light_direction{ 0 };
	DirectX::XMMATRIX vxl_transformation{ DirectX::XMMatrixIdentity() };
	DirectX::XMVECTOR remap_color{ 252.0f,0.0f,0.0f,1.0f };
	float section_buffer_size{ 0 };
protected:
	byte _padding[108];
};

struct renderer_state_data
{
	DirectX::XMMATRIX world{ DirectX::XMMatrixIdentity() };
	DirectX::XMVECTOR light_direction{ 0.2013022f,-0.9101138f,-0.3621709f,0.0f };
	DirectX::XMVECTOR remap_color{ 252.0f,0.0f,0.0f,1.0f };
	DirectX::XMVECTOR scale{ 1.0f,1.0f,1.0f,1.0f };
	DirectX::XMVECTOR bgcolor{ 0.0f,0.0f,1.0f,0.0f };
	DirectX::XMVECTOR canvas_dimension_extralight{ 256.0f,256.0f,0.2f,0.0f };
};

struct vxl_buffer_decl
{
#ifndef VXL_BYTE_TRANSFER
	uint32_t color = { 0 };
	uint32_t normal = { 0 };
	uint32_t x = { 0 };
	uint32_t y = { 0 };
	uint32_t z = { 0 };
#else
	uint8_t color = 0, normal = 0, x = 0, y = 0, z = 0;
#endif
};

struct upload_scene_states
{
	renderer_state_data data = {};
protected:
	byte _padding[256 - sizeof renderer_state_data] = {};
};

struct box_vertex_data
{
	struct face_vertex_data
	{
		DirectX::XMVECTOR _lt{ 0 }, _rt{ 0 }, _ld{ 0 }, _rt2{ 0 }, _ld2{ 0 }, _rd{ 0 };
	};

	face_vertex_data top, bottom, left, right, front, back;

	box_vertex_data();
};
class vpl_renderer
{
public:
	static const DXGI_FORMAT canvas_format = DXGI_FORMAT_R32G32_FLOAT;
	static const DXGI_FORMAT vxl_data_format =
#ifndef VXL_BYTE_TRANSFER
		DXGI_FORMAT_UNKNOWN;
#else
		DXGI_FORMAT_R8_UINT;
#endif
	static const DXGI_FORMAT vpl_data_format = DXGI_FORMAT_R8_UINT;
	static const DXGI_FORMAT palette_data_format = DXGI_FORMAT_R8_UINT;
	static const size_t normal_upload_buffer_idx = 0;
	static const size_t hva_constants_upload_buffer_idx = 1;
	static const size_t clear_target_buffer_idx = 2;
	static const size_t vertex_upload_buffer_idx = 3;
	static const size_t box_upload_buffer_idx = 4;
	static const size_t vpl_upload_buffer_idx = 5;
	static const size_t palette_upload_buffer_idx = 6;
	static const size_t pixel_upload_buffer_idx = 7;

	static const size_t box_vert_buffer_idx = 1;

	~vpl_renderer() = default;

	bool initialize(HWND output);
	void clear_renderer();
	//bool change_vxl_dimension(uint32_t x, uint32_t y, uint32_t z);
	bool load_vxl(const class vxl& vxl, const class hva& hva, const size_t frame, const bool clear = false);
	bool reload_hva(const class hva* hva[], const size_t frame[], const float prerotation[], const float offsets[], const size_t numhvas);
	bool load_vpl(const class vpl& vpl);
	bool load_pal(const class palette& pal);
	void clear_vxl_resources();
	bool init_pipeline_state();
	bool valid() const;

	bool begin_command();

	//fence must have been raised before calling this methods
	bool wait_for_completion();
	bool wait_for_sync();
	void execute_commands();
	bool transition_state(ID3D12Resource* resource, const D3D12_RESOURCE_STATES from, const D3D12_RESOURCE_STATES to);
	bool vxl_resource_initiated() const;
	bool bind_resource_table(const int resource_idx);
	bool render_loaded_vxl();
	
	bool render_gui(const bool clear_target = false);
	bool present_resource_initiated() const;
	bool present();
	bool clear_vxl_canvas();
	void set_light_dir(const DirectX::XMVECTOR& dir);
	void set_scale_factor(const DirectX::XMVECTOR& scale);
	void set_world(const DirectX::XMMATRIX& world);
	void set_bg_color(const DirectX::XMVECTOR& color);
	void set_remap(const struct color& color);
	void set_extra_light(const float extra);
	bool hardware_processing()const;
	size_t width() const;
	size_t height() const;
	bool resize_buffers();

	DirectX::XMVECTOR get_light_dir() const;
	DirectX::XMMATRIX get_world() const;
	DirectX::XMVECTOR get_scale_factor() const;
	DirectX::XMVECTOR get_bg_color() const;
	DirectX::XMVECTOR get_remap() const;
	std::vector<byte> front_buffer_data();
	std::vector<byte> render_target_data();

private:
	com_ptr<ID3D12Device> _device;
	com_ptr<ID3D12CommandQueue> _general_queue;
	com_ptr<ID3D12PipelineState> _pso, _render_pso,_box_pso;
	com_ptr<ID3D12RootSignature> _root_signature;
	
	//com_ptr<ID3D12DescriptorHeap> _uavs;
	com_ptr<ID3D12DescriptorHeap> _resource_descriptor_heaps, _render_target_views, _depth_stencil_views;

	renderer_state_data _states;
	d3d12_command_list _resource_commands;
	d3d12_swapchain _swapchain;
	d3d12_resource_set _depth_stencil_resource;
	d3d12_resource_set _vxl_resource;
	//std::vector<vxl_buffer_decl> _vxl_buffer_stroage;
	d3d12_resource_set _hva_resource;
	d3d12_resource_set _vpl_resource;
	d3d12_resource_set _palette_resource;
	d3d12_resource_set _upload_buffers;
	d3d12_resource_set _vertex_buffer;
	d3d12_resource_set _pixel_const_buffer;
	std::vector<vxl_cbuffer_data> _hva_buffer_storage;
	d3d12_fence _general_fence;
	bool _renderer_resource_dirty = { false };
	bool _box_rendered = { false };
	bool _hardware_processing = { false };

	//IMGUI STUFF
	com_ptr<ID3D12DescriptorHeap> _gui_descriptor_heaps;
};

