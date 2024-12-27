#include "d3d.h"
#include "hva.h"
#include "vxl.h"
#include "vpl.h"
#include "normals.h"
#include "resource.h"

#include <d3dcompiler.h>
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

inline uint32_t resource_pitch(const uint32_t data_pitch)
{
	return (data_pitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
}

void d3d12_render_target_set::discard()
{
	ref_device.reset();
	targets.clear();
	rtv_table.reset();
}

bool d3d12_render_target_set::valid() const
{
	return !targets.empty() && rtv_table.get();
}

bool d3d12_render_target_set::initailize(com_ptr<ID3D12Device> device, const size_t number_of_targets, const DXGI_FORMAT format, const size_t width, const size_t height)
{
	if (!device || !width || !height || !number_of_targets)
	{
		return false;
	}

	com_ptr<ID3D12Resource> target;
	D3D12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
	D3D12_HEAP_PROPERTIES pool_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear_value = { format,{0.0f,0.0f,0.0f,0.0f} };
	for (size_t i = 0; i < number_of_targets; i++)
	{
		if (FAILED(device->CreateCommittedResource(&pool_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value, IID_PPV_ARGS(&target))))
		{
			discard();
			break;
		}
		else
		{
			targets.push_back(target);
		}
	}

	if (!targets.empty())
	{
		com_ptr<ID3D12DescriptorHeap> table_heap;
		const size_t table_size = targets.size();
		const size_t rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
		rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtv_heap_desc.NodeMask = 0;
		rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_heap_desc.NumDescriptors = table_size;

		if (FAILED(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&table_heap))))
		{
			discard();
		}
		else
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE position(table_heap->GetCPUDescriptorHandleForHeapStart());
			for (size_t i = 0; i < targets.size(); i++)
			{
				device->CreateRenderTargetView(targets[i].get(), nullptr, position);
				position.Offset(rtv_increment);
			}

			rtv_table = table_heap;
			ref_device = device;
		}
	}

	return valid();
}

void d3d12_resource_set::discard()
{
	resources.clear();
	//ref_device.reset();
}

bool d3d12_resource_set::valid() const
{
	return !resources.empty();
}

bool d3d12_resource_set::add_empty_resource(com_ptr<ID3D12Device> device, const DXGI_FORMAT format, const D3D12_RESOURCE_DIMENSION dimension, const size_t width, const size_t height, const size_t depth_or_arraysize, const D3D12_HEAP_TYPE heap, const D3D12_RESOURCE_STATES initial_state)
{
	if (!device || !width || !height || !depth_or_arraysize)
	{
		return false;
	}

	com_ptr<ID3D12Resource> resource;
	D3D12_RESOURCE_DESC res_desc = {};
	res_desc.Dimension = dimension;
	res_desc.Width = width;
	res_desc.Height = height;
	res_desc.DepthOrArraySize = depth_or_arraysize;
	res_desc.Format = format;
	res_desc.MipLevels = 1;
	res_desc.SampleDesc = { 1,0 };
	res_desc.Flags = initial_state == D3D12_RESOURCE_STATE_DEPTH_WRITE ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : 
		(heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	res_desc.Layout = dimension == D3D12_RESOURCE_DIMENSION_BUFFER ?
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_HEAP_PROPERTIES pool_properties = CD3DX12_HEAP_PROPERTIES(heap);
	D3D12_CLEAR_VALUE clear_value = { format,{0.0f,0.0f,1.0f,1.0f} };

	if (res_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
		clear_value.DepthStencil = { 1.0f,0 };

	if (FAILED(device->CreateCommittedResource(&pool_properties, D3D12_HEAP_FLAG_NONE,
		&res_desc, initial_state,
		dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
		!((res_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
		(res_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
		? nullptr : &clear_value, IID_PPV_ARGS(&resource))))
	{
		return false;
	}

	resources.push_back(resource);
	return true;
}

d3d12_command_list::~d3d12_command_list()
{
	discard();
}

bool d3d12_command_list::initialize(com_ptr<ID3D12Device> device, const D3D12_COMMAND_LIST_TYPE type)
{
	if (!device)
	{
		return false;
	}

	discard();

	com_ptr<ID3D12GraphicsCommandList> command_list;
	com_ptr<ID3D12CommandAllocator> new_allocator;

	if (FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&new_allocator))) ||
		FAILED(device->CreateCommandList(0, type, new_allocator.get(), nullptr, IID_PPV_ARGS(&command_list))))
	{
		return false;
	}

	ref_device = device;
	commands = command_list;
	allocator = new_allocator;
	started = true;
	return valid();
}

com_ptr<ID3D12GraphicsCommandList> d3d12_command_list::record()
{
	if (started)
	{
		return commands;
	}

	return reset(), com_ptr<ID3D12GraphicsCommandList>();
}

bool d3d12_command_list::valid() const
{
	return commands && allocator;
}

bool d3d12_command_list::close()
{
	if (started && valid() && SUCCEEDED(commands->Close()))
	{
		started = false;
	}

	return !started;
}

bool d3d12_command_list::reset()
{
	return started = 
		started || (valid() && SUCCEEDED(allocator->Reset()) && SUCCEEDED(commands->Reset(allocator.get(), nullptr)));
}

void d3d12_command_list::discard()
{
	close();
	reset();
	ref_device.reset();
	commands.reset();
	allocator.reset();
	started = false;
}

d3d12_fence::~d3d12_fence()
{
	discard();
}

void d3d12_fence::reset_fence()
{
	fence_value = 0;
	fence.reset();
}

bool d3d12_fence::initialize(com_ptr<ID3D12Device> device, com_ptr<ID3D12CommandQueue> queue)
{
	if (!device || !queue)
	{
		return false;
	}

	reset_fence();

	com_ptr<ID3D12Fence> new_fence;
	HANDLE sync = INVALID_HANDLE_VALUE;
	if (FAILED(device->CreateFence(fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&new_fence))))
	{
		return false;
	}

	/*sync = CreateEventA(nullptr, false, false, nullptr);
	if (sync == INVALID_HANDLE_VALUE)
	{
		CloseHandle(sync);
		return false;
	}*/

	fence = new_fence;
	sync_event = sync;
	ref_device = device;
	ref_queue = queue;
	return true;
}

bool d3d12_fence::valid() const
{
	return fence/* && sync_event != INVALID_HANDLE_VALUE*/;
}

bool d3d12_fence::raise_fence()
{
	if (!valid())
	{
		return false;
	}

	if (sync_event != INVALID_HANDLE_VALUE)
		CloseHandle(sync_event);

	sync_event = CreateEventA(nullptr, false, false, nullptr);
	if (sync_event == INVALID_HANDLE_VALUE)
		return false;

	uint64_t new_fence_value = fence_value + 1;
	HRESULT hr = S_OK;
	if (fence->GetCompletedValue() < new_fence_value && SUCCEEDED(hr = fence->SetEventOnCompletion(new_fence_value, sync_event)))
	{
		fence_value = new_fence_value;
		return true;
	}

	return false;
}

bool d3d12_fence::await_completion(const size_t time)//[1]
{
	if (!valid())
		return false;

	//GPU side fence
	HRESULT hr = S_OK;
	if (FAILED(hr = ref_queue->Signal(fence.get(), fence_value)))
	{
		return false;
	}

	WaitForSingleObject(sync_event, time);
	return true;
}

void d3d12_fence::discard()
{
	ref_device.reset();
	ref_queue.reset();
	fence.reset();
	fence_value = 0;

	CloseHandle(sync_event);
	sync_event = INVALID_HANDLE_VALUE;
}

bool vpl_renderer::initialize(HWND output)
{
	/*if (output == NULL)
		return false;*/

	com_ptr<IDXGIFactory6> factory;
	com_ptr<IDXGIAdapter1> adapter;
	UINT dxgiFactoryFlags = NULL;
	bool hardware_processing = false;

#ifdef _DEBUG
	{
		com_ptr<ID3D12Debug> debug_control;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_control)))) {
			debug_control->EnableDebugLayer();

			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)))) {
		LOG(ERROR) << "Factory creation.\n";
		return false;
	}

	for (UINT adapter_idx = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(
		adapter_idx,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
		IID_PPV_ARGS(&adapter)));
		adapter_idx++) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
		{
			hardware_processing = true;
			break;
		}
	}

	if (!hardware_processing && FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
	{
		LOG(ERROR) << "WARP adapter enumeration.\n";
		return false;
	}
	
	com_ptr<ID3D12Device> device;
	if (FAILED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
		LOG(ERROR) << "Device creation.\n";
		return false;
	}

	com_ptr<ID3D12CommandQueue> command_queue; 
	D3D12_COMMAND_QUEUE_DESC queue_descs = {};
	queue_descs.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_descs.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(device->CreateCommandQueue(&queue_descs, IID_PPV_ARGS(&command_queue)))) {
		LOG(ERROR) << "Queue creation.\n";
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC resource_heap = {}, render_target_view = {}, depth_stencil_view = {}, gui_resource_view = {};
	com_ptr<ID3D12DescriptorHeap> resources_views, render_target_views, depth_stencil_views, gui_resource_views;
	resource_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	resource_heap.NumDescriptors = 1000;
	resource_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	render_target_view.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	render_target_view.NodeMask = 0;
	render_target_view.NumDescriptors = 3;
	render_target_view.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	depth_stencil_view.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	depth_stencil_view.NumDescriptors = 1;
	depth_stencil_view.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	gui_resource_view.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	gui_resource_view.NumDescriptors = 1;
	gui_resource_view.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(device->CreateDescriptorHeap(&resource_heap, IID_PPV_ARGS(&resources_views))))
	{
		LOG(ERROR) << "Resource view heap creation.\n";
		return false;
	}

	if (FAILED(device->CreateDescriptorHeap(&render_target_view, IID_PPV_ARGS(&render_target_views))))
	{
		LOG(ERROR) << "RTV heap creation.\n";
		return false;
	}

	if (FAILED(device->CreateDescriptorHeap(&depth_stencil_view, IID_PPV_ARGS(&depth_stencil_views))))
	{
		LOG(ERROR) << "DSV heap creation.\n";
		return false;
	}

	if (FAILED(device->CreateDescriptorHeap(&gui_resource_view, IID_PPV_ARGS(&gui_resource_views))))
	{
		LOG(ERROR) << "GUI resource heap creation.\n";
	}

	d3d12_command_list resource_commands = {};
	d3d12_swapchain swapchain = {};
	d3d12_resource_set vxl_resources = {}, hva_resources = {}, upload_heaps = {}, resource_for_calculation = {}, vertex_buffers = {}, pix_const_buffer = {}, depth_stencil_buffer = {};
	d3d12_fence general_fence = {};
	if (!resource_commands.initialize(device, D3D12_COMMAND_LIST_TYPE_DIRECT) ||
		!swapchain.initialize(command_queue,factory,output) ||
		!hva_resources.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, sizeof game_normals, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST) ||
		!pix_const_buffer.add_empty_resource(device,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_DIMENSION_BUFFER,sizeof upload_scene_states,1,1,D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_SHADER_RESOURCE)||
		!vertex_buffers.add_empty_resource(device,DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, sizeof canvas_vertecies_data, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST)||
		!vertex_buffers.add_empty_resource(device,DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, sizeof box_vertex_data, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST)||
		!vxl_resources.add_empty_resource(device, canvas_format, D3D12_RESOURCE_DIMENSION_TEXTURE2D, width(), height(), 1) ||
		!depth_stencil_buffer.add_empty_resource(device,DXGI_FORMAT_D24_UNORM_S8_UINT,D3D12_RESOURCE_DIMENSION_TEXTURE2D, width(), height(),1,D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE)||
		!general_fence.initialize(device, command_queue))
	{
		return false;
	}
	
	//static const size_t normal_upload_buffer_idx = 0;
	//static const size_t hva_constants_upload_buffer_idx = 1;
	//static const size_t clear_target_buffer_idx = 2;
	if (!resource_for_calculation.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER,
		sizeof vxl_cbuffer_data, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_SHADER_RESOURCE) ||
		!resource_for_calculation.add_empty_resource(device, vpl_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE1D,
		32 * 256, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST)|| 
		!resource_for_calculation.add_empty_resource(device, palette_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE1D,
		3 * 256, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST))
	{
		return false;
	}

	const size_t hva_imsize = GetRequiredIntermediateSize(hva_resources.resources[0].get(), 0, 1);
	const size_t hva_const_imsize = GetRequiredIntermediateSize(resource_for_calculation.resources[0].get(), 0, 1);
	const size_t clear_target_size = GetRequiredIntermediateSize(vxl_resources.resources[0].get(), 0, 1);
	const size_t vert_buffer_size = GetRequiredIntermediateSize(vertex_buffers.resources[0].get(), 0, 1);
	const size_t vert_buffer2_size = GetRequiredIntermediateSize(vertex_buffers.resources[box_vert_buffer_idx].get(), 0, 1);
	const size_t vpl_buffer_upload_size = GetRequiredIntermediateSize(resource_for_calculation.resources[1].get(), 0, 1);
	const size_t palette_upload_size = GetRequiredIntermediateSize(resource_for_calculation.resources[2].get(), 0, 1);
	const size_t scene_upload_size = GetRequiredIntermediateSize(pix_const_buffer.resources[0].get(), 0, 1);

	if (!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, hva_imsize, 1, 1,
		D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, hva_const_imsize, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, clear_target_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, vert_buffer_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, vert_buffer2_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, vpl_buffer_upload_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, palette_upload_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) ||
		!upload_heaps.add_empty_resource(device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, scene_upload_size, 1, 1,
			D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ))
	{
		return false;
	}
	
	//prefill render target
	float* upload_data = nullptr;
	if (FAILED(upload_heaps.resources[clear_target_buffer_idx]->Map(0, nullptr, reinterpret_cast<void**>(&upload_data))))
	{
		return false;
	}

	for (size_t y = 0; y < height(); y++)
	{
		for (size_t x = 0; x < width() * 2; x++)
			upload_data[x] = 0.0f;
		upload_data += resource_pitch(width() * 8) / sizeof(float);
	}

	upload_heaps.resources[clear_target_buffer_idx]->Unmap(0, nullptr);
	
	if (!ImGui_ImplDX12_Init(device.get(), 3, DXGI_FORMAT_R8G8B8A8_UNORM, gui_resource_views.get(),
		gui_resource_views->GetCPUDescriptorHandleForHeapStart(), gui_resource_views->GetGPUDescriptorHandleForHeapStart()))
	{
		LOG(ERROR) << "Failed to invoke ImGui dx12 backend.\n";
		return false;
	}

	_device = device;
	_resource_commands = resource_commands;
	_swapchain = swapchain;
	//_target_surface = render_target;
	_vxl_resource = vxl_resources;
	_hva_resource = hva_resources;
	_upload_buffers = upload_heaps;
	_general_fence = general_fence;
	_general_queue = command_queue;
	_pixel_const_buffer = pix_const_buffer;
	_resource_descriptor_heaps = resources_views;
	_render_target_views = render_target_views;
	_depth_stencil_views = depth_stencil_views;
	_depth_stencil_resource = depth_stencil_buffer;
	_vertex_buffer = vertex_buffers;
	_hardware_processing = hardware_processing;
	_gui_descriptor_heaps = gui_resource_views;

	if (!init_pipeline_state())
	{
		clear_renderer();
		return false;
	}

	return true;
}

void vpl_renderer::clear_renderer()
{
	wait_for_sync();

	ImGui_ImplDX12_Shutdown();

	_resource_commands.discard();
	_swapchain.discard();
	_depth_stencil_resource.discard();
	_vxl_resource.discard();
	//_vxl_buffer_stroage.clear();
	_hva_resource.discard();
	_vpl_resource.discard();
	_upload_buffers.discard();
	_palette_resource.discard();
	_pixel_const_buffer.discard();
	_hva_buffer_storage.clear();
	_general_fence.reset_fence();
	_resource_descriptor_heaps.reset();
	_pso.reset();
	_render_pso.reset();
	_box_pso.reset();
	_render_target_views.reset();
	_depth_stencil_views.reset();
	_vertex_buffer.discard();
	_device.reset();
	_general_queue.reset();
	_renderer_resource_dirty = false;
	_box_rendered = false;
	_hardware_processing = false;
	_gui_descriptor_heaps.reset();
}
//
//bool vpl_renderer::change_vxl_dimension(uint32_t x, uint32_t y, uint32_t z)
//{
//	if(!valid())
//		return false;
//
//	clear_vxl_resources();
//	
//	if (!_vxl_resource.add_empty_resource(_device, vxl_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE3D, x, y, z) ||
//		!_vxl_resource.add_empty_resource(_device, vxl_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE3D, x, y, z,
//			D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ))
//	{
//		clear_vxl_resources();
//		return false;
//	}
//
//	return true;
//}

bool vpl_renderer::load_vxl(const vxl& vxl, const hva& hva, const size_t frame, const bool clear)
{
	if (!valid() || !vxl.is_loaded() || !hva.is_loaded() || vxl.limb_count() != hva.section_count())
		return false;

	if(clear)
		clear_vxl_resources();

	const size_t limbs = vxl.limb_count();
	for (size_t i = 0; i < limbs; i++)
	{
		const vxl_limb_tailer* tailer = vxl.limb_tailer(i);
		const vxlmatrix matrix = *hva.matrix(frame % hva.frame_count(), i);
		//const size_t maximum_vxl_buffer_size = sizeof vxl_buffer_decl * tailer->xsize * tailer->ysize * tailer->zsize;
		com_ptr<ID3D12Resource> temp_resources;

		std::vector<vxl_buffer_decl> vxl_data = {};
		vxl_buffer_decl temp_decl = {};
		voxel temp_vox = {};
		for (size_t z = 0; z < tailer->zsize; z++)
		{
			for (size_t y = 0; y < tailer->ysize; y++)
			{
				for (size_t x = 0; x < tailer->xsize; x++)
				{
					temp_vox = vxl.voxel_rh(i, x, y, z);
					if (temp_vox.color)
					{
						temp_decl.color = temp_vox.color;
						temp_decl.normal = temp_vox.normal;
						temp_decl.x = x;
						temp_decl.y = y;
						temp_decl.z = z;
						vxl_data.push_back(temp_decl);
					}
				}
			}
		}

		//considering empty resources
		if (vxl_data.empty())
			vxl_data.push_back({ 0,0,0,0,0 });

		const size_t buffer_size = sizeof vxl_buffer_decl * vxl_data.size();
		if (_vxl_resource.add_empty_resource(_device, vxl_data_format, 
#ifndef VXL_BYTE_TRANSFER
			D3D12_RESOURCE_DIMENSION_BUFFER,
#else
			D3D12_RESOURCE_DIMENSION_TEXTURE2D,
#endif
			std::min(buffer_size ,static_cast<size_t>(16000u)), buffer_size / 16000u + 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST) &&
			_hva_resource.add_empty_resource(_device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER,
				sizeof vxl_cbuffer_data, 1, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_SHADER_RESOURCE))
		{
			const auto new_resource = _vxl_resource.resources.back();
			const auto imm_size = GetRequiredIntermediateSize(new_resource.get(), 0u, 1u);

			vxl_cbuffer_data tempdata = {};
			tempdata.vxl_dimension.vector4_f32[0] = tailer->xsize;
			tempdata.vxl_dimension.vector4_f32[1] = tailer->ysize;
			tempdata.vxl_dimension.vector4_f32[2] = tailer->zsize;
			tempdata.vxl_dimension.vector4_f32[3] = tailer->scale;

			tempdata.vxl_minbound.vector4_f32[0] = tailer->min_bounds[0];
			tempdata.vxl_minbound.vector4_f32[1] = tailer->min_bounds[1];
			tempdata.vxl_minbound.vector4_f32[2] = tailer->min_bounds[2];
			tempdata.vxl_minbound.vector4_f32[3] = vxl_data.size();
			tempdata.vxl_maxbound.vector4_f32[0] = tailer->max_bounds[0];
			tempdata.vxl_maxbound.vector4_f32[1] = tailer->max_bounds[1];
			tempdata.vxl_maxbound.vector4_f32[2] = tailer->max_bounds[2];
			tempdata.vxl_maxbound.vector4_f32[3] = 0.0f;

			tempdata.light_direction = _states.light_direction;

			tempdata.vxl_transformation.m[0][0] = matrix._data[0][0];
			tempdata.vxl_transformation.m[0][1] = matrix._data[1][0];
			tempdata.vxl_transformation.m[0][2] = matrix._data[2][0];
			tempdata.vxl_transformation.m[0][3] = 0.0f;
			tempdata.vxl_transformation.m[1][0] = matrix._data[0][1];
			tempdata.vxl_transformation.m[1][1] = matrix._data[1][1];
			tempdata.vxl_transformation.m[1][2] = matrix._data[2][1];
			tempdata.vxl_transformation.m[1][3] = 0.0f;
			tempdata.vxl_transformation.m[2][0] = matrix._data[0][2];
			tempdata.vxl_transformation.m[2][1] = matrix._data[1][2];
			tempdata.vxl_transformation.m[2][2] = matrix._data[2][2];
			tempdata.vxl_transformation.m[2][3] = 0.0f;
			tempdata.vxl_transformation.m[3][0] = matrix._data[0][3];
			tempdata.vxl_transformation.m[3][1] = matrix._data[1][3];
			tempdata.vxl_transformation.m[3][2] = matrix._data[2][3];
			tempdata.vxl_transformation.m[3][3] = 1.0f;

			tempdata.section_buffer_size = buffer_size;
			_hva_buffer_storage.push_back(tempdata);

			D3D12_HEAP_PROPERTIES temp_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			D3D12_RESOURCE_DESC temp_desc = CD3DX12_RESOURCE_DESC::Buffer(imm_size);
			_device->CreateCommittedResource(&temp_heap, D3D12_HEAP_FLAG_NONE, &temp_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&temp_resources));
			if (/*vxl_limb_buffer && */temp_resources)
			{
#ifdef _DEBUG
				new_resource->SetName(L"new resource");
				temp_resources->SetName(L"temp buffer");
#endif

				if (begin_command())
				{
					D3D12_SUBRESOURCE_DATA subres = {};
					subres.pData = vxl_data.data();
					subres.RowPitch = std::min(buffer_size, static_cast<size_t>(16000u));
					subres.SlicePitch = buffer_size;

					UpdateSubresources(_resource_commands.commands.get(), new_resource.get(), temp_resources.get(), 0, 0, 1, &subres);
					//transition_state(new_resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					execute_commands();
					wait_for_completion();
				}
			}
		}
	}

	if (_vxl_resource.resources.size() - 1 != _hva_buffer_storage.size() ||
		_vxl_resource.resources.size() != _hva_resource.resources.size())
	{
		clear_vxl_resources();
		return false;
	}

	return _renderer_resource_dirty = true;
}

bool vpl_renderer::load_vpl(const vpl& vpl)
{
	if (!valid() || !vpl.is_loaded())
		return false;

	_vpl_resource.discard();
	if (_vpl_resource.add_empty_resource(_device, vpl_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE1D, 32 * 256, 1, 1,
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST))
	{
		const com_ptr<ID3D12Resource>& destination = _vpl_resource.resources[0];
		const com_ptr<ID3D12Resource>& upload = _upload_buffers.resources[vpl_upload_buffer_idx];
		if (begin_command())
		{
			D3D12_SUBRESOURCE_DATA subres = {};
			subres.pData = vpl.data();
			subres.RowPitch = subres.SlicePitch = 32 * 256;
			UpdateSubresources(_resource_commands.commands.get(), destination.get(), upload.get(), 0, 0, 1, &subres);
			execute_commands();
			return _renderer_resource_dirty = wait_for_completion();
		}
	}

	return false;
}

bool vpl_renderer::load_pal(const palette& palette)
{
	if (!valid() || !palette.is_loaded())
		return false;

	_palette_resource.discard();
	if (_palette_resource.add_empty_resource(_device, vpl_data_format, D3D12_RESOURCE_DIMENSION_TEXTURE1D, 3 * 256, 1, 1,
		D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST))
	{
		const com_ptr<ID3D12Resource>& destination = _palette_resource.resources[0];
		const com_ptr<ID3D12Resource>& upload = _upload_buffers.resources[palette_upload_buffer_idx];
		if (begin_command())
		{
			D3D12_SUBRESOURCE_DATA subres = {};
			subres.pData = palette.entry();
			subres.RowPitch = subres.SlicePitch = 3 * 256;
			UpdateSubresources(_resource_commands.commands.get(), destination.get(), upload.get(), 0, 0, 1, &subres);
			execute_commands();
			return _renderer_resource_dirty = wait_for_completion();
		}
	}

	return false;
}

bool vpl_renderer::reload_hva(const hva* hvas[], const size_t frames[], const float prerotation[], const float offsets[], const size_t numhvas)
{
	if (!valid() || !numhvas)
		return false;

	//pre check
	size_t total_data_set = 0;
	for (size_t i = 0; i < numhvas; i++)
	{
		const hva* pv = hvas[i];
		if (!pv)
			return false;
		else
			total_data_set += pv->section_count();
	}

	if (total_data_set != _hva_buffer_storage.size())
	{
		return false;
	}

	for (size_t i = 0, loading_section = 0; i < numhvas; i++)
	{
		const hva& hva = *hvas[i];
		const size_t real_frame = hva.frame_count() ? (frames[i] % hva.frame_count()) : 0;/* ? 0 : frames[i];*/
		DirectX::XMMATRIX prerot = DirectX::XMMatrixIdentity();

		if (prerotation)
		{
			__try
			{
				prerot = DirectX::XMMatrixRotationZ(prerotation[i]);
			}
			__except (1)
			{
				LOG(ERROR) << __FUNCTION__": pre rotation vector access violation.\n";
			}
		}

		for (size_t s = 0; s < hva.section_count(); s++)
		{
			vxl_cbuffer_data& tempdata = _hva_buffer_storage[loading_section++];
			const vxlmatrix& matrix = *hva.matrix(real_frame, s);
			tempdata.light_direction = _states.light_direction;
			tempdata.vxl_transformation.m[0][0] = matrix._data[0][0];
			tempdata.vxl_transformation.m[0][1] = matrix._data[1][0];
			tempdata.vxl_transformation.m[0][2] = matrix._data[2][0];
			tempdata.vxl_transformation.m[1][0] = matrix._data[0][1];
			tempdata.vxl_transformation.m[1][1] = matrix._data[1][1];
			tempdata.vxl_transformation.m[1][2] = matrix._data[2][1];
			tempdata.vxl_transformation.m[2][0] = matrix._data[0][2];
			tempdata.vxl_transformation.m[2][1] = matrix._data[1][2];
			tempdata.vxl_transformation.m[2][2] = matrix._data[2][2];
			tempdata.vxl_transformation.m[3][0] = matrix._data[0][3];
			tempdata.vxl_transformation.m[3][1] = matrix._data[1][3];
			tempdata.vxl_transformation.m[3][2] = matrix._data[2][3];

			if(offsets)
				tempdata.vxl_maxbound.vector4_f32[3] = offsets[i];

			tempdata.vxl_transformation *= prerot;
		}
	}

	return true;
}

//if i == -1, bind fixed resource () / update only when resource reloaded
//else bind vxl/hva resource only / called when rendering any section
bool vpl_renderer::bind_resource_table(const int resource_idx)
{
	if (!valid() || !vxl_resource_initiated() || !present_resource_initiated())
		return false;

	//resource binding
	const com_ptr<ID3D12Resource>& canvaz_resource = _vxl_resource.resources[0];
	const com_ptr<ID3D12Resource>& vpl_resource = _vpl_resource.resources[0];
	const com_ptr<ID3D12Resource>& vxl_resource = _vxl_resource.resources[resource_idx + 1];
	const com_ptr<ID3D12Resource>& hva_resource = _hva_resource.resources[resource_idx + 1];
	const com_ptr<ID3D12Resource>& pal_resource = _palette_resource.resources[0];
	const com_ptr<ID3D12Resource>& pix_resource = _pixel_const_buffer.resources[0];
	const vxl_cbuffer_data& data = _hva_buffer_storage[resource_idx == -1 ? 0 : resource_idx];

	UINT handle_offset = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE resource_table_handle(_resource_descriptor_heaps->GetCPUDescriptorHandleForHeapStart());
	D3D12_UNORDERED_ACCESS_VIEW_DESC canvaz_view = {}; /*vxl_data_view = {}, */
	D3D12_SHADER_RESOURCE_VIEW_DESC	vpl_data_view = {}, pal_data_view = {};
#ifndef VXL_BYTE_TRANSFER
	D3D12_SHADER_RESOURCE_VIEW_DESC vxl_data_view = {};
#else
	D3D12_SHADER_RESOURCE_VIEW_DESC vxl_data_view = {};
#endif
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_view = {}, normal_table_desc = {}, pix_upload_view = {};
	canvaz_view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	canvaz_view.Format = canvas_format;
	canvaz_view.Texture2D.MipSlice = 0;
	canvaz_view.Texture2D.PlaneSlice = 0;

#ifndef VXL_BYTE_TRANSFER
	vxl_data_view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	vxl_data_view.Format = vxl_data_format;
	vxl_data_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	vxl_data_view.Buffer.FirstElement = 0;
	vxl_data_view.Buffer.NumElements = static_cast<UINT>(data.vxl_minbound.vector4_f32[3]);
	vxl_data_view.Buffer.StructureByteStride = sizeof vxl_buffer_decl;
#else
	vxl_data_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	vxl_data_view.Format = vxl_data_format;
	vxl_data_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	vxl_data_view.Texture2D.MipLevels = 1;
	vxl_data_view.Texture2D.PlaneSlice = 0;
	vxl_data_view.Texture2D.MostDetailedMip = 0;
#endif

	vpl_data_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
	vpl_data_view.Format = vpl_data_format;
	vpl_data_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	vpl_data_view.Texture1D.MipLevels = 1;
	vpl_data_view.Texture1D.MostDetailedMip = 0;

	pal_data_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
	pal_data_view.Format = palette_data_format;
	pal_data_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	pal_data_view.Texture1D.MipLevels = 1;
	pal_data_view.Texture1D.MostDetailedMip = 0;

	cbv_view.BufferLocation = hva_resource->GetGPUVirtualAddress();
	cbv_view.SizeInBytes = resource_pitch(sizeof vxl_cbuffer_data);
	normal_table_desc.BufferLocation = _hva_resource.resources[0]->GetGPUVirtualAddress();
	normal_table_desc.SizeInBytes = resource_pitch(sizeof game_normals);
	pix_upload_view.BufferLocation = pix_resource->GetGPUVirtualAddress();
	pix_upload_view.SizeInBytes = resource_pitch(sizeof upload_scene_states);

	resource_table_handle.Offset(handle_offset);
	if (resource_idx == -1)_device->CreateUnorderedAccessView(canvaz_resource.get(), nullptr, &canvaz_view, resource_table_handle);//u1
	resource_table_handle.Offset(handle_offset);
	if (resource_idx != -1)_device->CreateShaderResourceView(vxl_resource.get(), &vxl_data_view, resource_table_handle);//t2
	resource_table_handle.Offset(handle_offset);
	if (resource_idx == -1)_device->CreateShaderResourceView(vpl_resource.get(), &vpl_data_view, resource_table_handle);//u3
	resource_table_handle.Offset(handle_offset);
	if (resource_idx == -1)_device->CreateConstantBufferView(&normal_table_desc, resource_table_handle);//b4
	resource_table_handle.Offset(handle_offset);
	if (resource_idx != -1)_device->CreateConstantBufferView(&cbv_view, resource_table_handle);//b5
	resource_table_handle.Offset(handle_offset);
	/*if (resource_idx == -1)*/_device->CreateShaderResourceView(pal_resource.get(), &pal_data_view, resource_table_handle);//u6
	resource_table_handle.Offset(handle_offset);
	if (resource_idx == -1)_device->CreateConstantBufferView(&pix_upload_view, resource_table_handle);//b7

	if (resource_idx == -1)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(_render_target_views->GetCPUDescriptorHandleForHeapStart());
		for (size_t i = 0; i < 3; i++)
		{
			_device->CreateRenderTargetView(_swapchain.targets[i].get(), nullptr, rtv_handle);
			rtv_handle.Offset(_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(_depth_stencil_views->GetCPUDescriptorHandleForHeapStart());
		_device->CreateDepthStencilView(_depth_stencil_resource.resources[0].get(), nullptr, dsv_handle);
	}
	return true;
}

bool vpl_renderer::present()
{
	if (!valid() || !present_resource_initiated())
		return false;

	if (_renderer_resource_dirty)
	{
		bind_resource_table(-1);
		_renderer_resource_dirty = false;
	}

	if (begin_command())
	{
		if (!_box_rendered)
		{
			com_ptr<ID3D12GraphicsCommandList>& command_list = _resource_commands.commands;
			D3D12_VIEWPORT viewport = { 0,0,width(),height(),0.0f,1.0f };
			D3D12_RECT scissor_rect = { 0,0,width(),height() };

			ID3D12DescriptorHeap* heaps[] = { _resource_descriptor_heaps.get() };
			command_list->SetPipelineState(_render_pso.get());
			command_list->SetGraphicsRootSignature(_root_signature.get());
			command_list->SetDescriptorHeaps(_countof(heaps), heaps);
			command_list->SetGraphicsRootDescriptorTable(0, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			command_list->SetGraphicsRootDescriptorTable(1, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			command_list->SetGraphicsRootDescriptorTable(2, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			//prepare view port
			command_list->RSSetViewports(1, &viewport);
			command_list->RSSetScissorRects(1, &scissor_rect);

			upload_scene_states state_constants = {};
			state_constants.data = _states;
			D3D12_SUBRESOURCE_DATA subres = {};
			subres.pData = &state_constants;
			subres.RowPitch = subres.SlicePitch = sizeof state_constants;
			transition_state(_pixel_const_buffer.resources[0].get(), D3D12_RESOURCE_STATE_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			UpdateSubresources(command_list.get(), _pixel_const_buffer.resources[0].get(), _upload_buffers.resources[pixel_upload_buffer_idx].get(), 0, 0, 1, &subres);
			transition_state(_pixel_const_buffer.resources[0].get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);

			const size_t buffer_idx = _swapchain.current_idx;
			const size_t rtv_increment = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			D3D12_CPU_DESCRIPTOR_HANDLE target =
				CD3DX12_CPU_DESCRIPTOR_HANDLE(_render_target_views->GetCPUDescriptorHandleForHeapStart(), buffer_idx, rtv_increment);
			command_list->OMSetRenderTargets(1, &target, false, nullptr);

			command_list->ClearRenderTargetView(target, _states.bgcolor.vector4_f32, 0, nullptr);

			//prepare input assembly
			D3D12_VERTEX_BUFFER_VIEW vtb_view = {};
			vtb_view.BufferLocation = _vertex_buffer.resources[0]->GetGPUVirtualAddress();
			vtb_view.SizeInBytes = sizeof canvas_vertecies_data::vertex;
			vtb_view.StrideInBytes = sizeof canvas_vertecies_data::vertex[0];

			command_list->IASetVertexBuffers(0, 1, &vtb_view);
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			command_list->DrawInstanced(3, 1, 0, 0);

			transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		}
		else
		{
			//_box_rendered = false;
		}

		execute_commands();
		if (wait_for_completion())
		{
			if (FAILED(_swapchain.swapchain->Present(1, 0)))
				return false;

			_swapchain.current_idx = _swapchain.swapchain->GetCurrentBackBufferIndex();
			return true;
		}
	}
	return false;
}

bool vpl_renderer::vxl_resource_initiated() const
{
	return !(_vxl_resource.resources.size() < 2 || _vpl_resource.resources.empty() ||
		_vxl_resource.resources.size() - 1 != _hva_buffer_storage.size() ||
		_vxl_resource.resources.size() != _hva_resource.resources.size() ||
		!_vpl_resource.valid());
}


bool vpl_renderer::present_resource_initiated() const
{
	return _palette_resource.valid() && _pixel_const_buffer.valid();
}

void vpl_renderer::clear_vxl_resources()
{
	if (valid())
	{
		_vxl_resource.resources.erase(_vxl_resource.resources.begin() + 1, _vxl_resource.resources.end());
		_hva_buffer_storage.clear();
		_hva_resource.resources.erase(_hva_resource.resources.begin() + 1, _hva_resource.resources.end());
		_hva_buffer_storage.clear();
	}
}

bool vpl_renderer::init_pipeline_state()
{
	if(!_device.get())
		return false;

	D3D12_DESCRIPTOR_RANGE uav_range[1];
	uav_range[0].BaseShaderRegister = 0;
	uav_range[0].NumDescriptors = 1000;
	uav_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uav_range[0].RegisterSpace = 0;
	uav_range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_DESCRIPTOR_RANGE srv_range[1];
	srv_range[0].BaseShaderRegister = 0;
	srv_range[0].NumDescriptors = 1000;
	srv_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srv_range[0].RegisterSpace = 0;
	srv_range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_DESCRIPTOR_RANGE cbv_range[1];
	cbv_range[0].BaseShaderRegister = 0;
	cbv_range[0].NumDescriptors = 1000;
	cbv_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	cbv_range[0].RegisterSpace = 0;
	cbv_range[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER params[3];
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[0].DescriptorTable.NumDescriptorRanges = _countof(uav_range);
	params[0].DescriptorTable.pDescriptorRanges = uav_range;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[1].DescriptorTable.NumDescriptorRanges = _countof(cbv_range);
	params[1].DescriptorTable.pDescriptorRanges = cbv_range;
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[2].DescriptorTable.NumDescriptorRanges = _countof(srv_range);
	params[2].DescriptorTable.pDescriptorRanges = srv_range;
	/*params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[3].Constants.Num32BitValues = 4;
	params[3].Constants.RegisterSpace = 1;
	params[3].Constants.ShaderRegister = 0;*/
	D3D12_STATIC_SAMPLER_DESC static_samp = {};

	//not used
	static_samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	static_samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	static_samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	static_samp.ShaderRegister = 0;
	static_samp.RegisterSpace = 0;
	static_samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC root_desc = {};
	root_desc.NumParameters = _countof(params);
	root_desc.pParameters = params;
	root_desc.NumStaticSamplers = 1;
	root_desc.pStaticSamplers = &static_samp;
	root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	com_ptr<ID3D12RootSignature> root_signature;
	com_ptr<ID3DBlob> serialized_root_signature_code, error;
	if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_signature_code, &error))) 
	{
		LOG(ERROR) << "Root signature compilation.\n";
		if (error)
			LOG(ERROR) << (char*)error->GetBufferPointer();
		return false;
	}

	error.reset();
	if (FAILED(_device->CreateRootSignature(NULL, serialized_root_signature_code->GetBufferPointer(), serialized_root_signature_code->GetBufferSize(), IID_PPV_ARGS(&root_signature)))) 
	{
		LOG(ERROR) << "Root signature creation.\n";
		return false;
	}

	void* shader_code = nullptr;
	size_t res_size = 0;
	if (auto resource = FindResource(NULL, MAKEINTRESOURCE(IDR_SHADER1), TEXT("SHADER")))
	{
		res_size = SizeofResource(NULL, resource);
		if (auto res_heap = LoadResource(NULL, resource))
			shader_code = LockResource(res_heap);
	}

	com_ptr<ID3DBlob> compute_shader, pixel_shader, vertex_shader, box_vshader, box_pshader;
#ifdef _DEBUG
	UINT compile_flag = D3DCOMPILE_DEBUG;
#else
	UINT compile_flag = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
	if (FAILED(D3DCompile(shader_code, res_size, nullptr, nullptr, nullptr, "main", "cs_5_1", compile_flag, 0, &compute_shader, &error)))
	{
		LOG(ERROR) << "Compiling shaders.\n" << (LPSTR)error->GetBufferPointer() << ".\n";
		return false;
	}
	error.reset();
	if (FAILED(D3DCompile(shader_code, res_size, nullptr, nullptr, nullptr, "pmain", "ps_5_1", compile_flag, 0, &pixel_shader, &error)))
	{
		LOG(ERROR) << "Compiling shaders.\n" << (LPSTR)error->GetBufferPointer() << ".\n";
		return false;
	}
	error.reset();
	if (FAILED(D3DCompile(shader_code, res_size, nullptr, nullptr, nullptr, "vmain", "vs_5_1", compile_flag, 0, &vertex_shader, &error)))
	{
		LOG(ERROR) << "Compiling shaders.\n" << (LPSTR)error->GetBufferPointer() << ".\n";
		return false;
	}
	error.reset();
	if (FAILED(D3DCompile(shader_code, res_size, nullptr, nullptr, nullptr, "box_vmain", "vs_5_1", compile_flag, 0, &box_vshader, &error)))
	{
		LOG(ERROR) << "Compiling shaders.\n" << (LPSTR)error->GetBufferPointer() << ".\n";
		return false;
	}
	error.reset();
	if (FAILED(D3DCompile(shader_code, res_size, nullptr, nullptr, nullptr, "box_pmain", "ps_5_1", compile_flag, 0, &box_pshader, &error)))
	{
		LOG(ERROR) << "Compiling shaders.\n" << (LPSTR)error->GetBufferPointer() << ".\n";
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pipeline = {};
	compute_pipeline.CS = CD3DX12_SHADER_BYTECODE(compute_shader.get());
	compute_pipeline.pRootSignature = root_signature.get();

	D3D12_INPUT_ELEMENT_DESC input_assembly[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_INPUT_ELEMENT_DESC box_vert_assembly[] =
	{
		{ "POSITIOn", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = {}, boxpipe = {};
	pipeline.InputLayout = { input_assembly,_countof(input_assembly) };
	pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.get());
	pipeline.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.get());
	pipeline.SampleMask = UINT_MAX;
	pipeline.SampleDesc = { 1,0 };
	pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline.NumRenderTargets = 1;
	pipeline.pRootSignature = root_signature.get();
	pipeline.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
	pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pipeline.DepthStencilState.DepthEnable = FALSE;
	pipeline.DepthStencilState.StencilEnable = FALSE;
	pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	boxpipe.InputLayout = { box_vert_assembly,_countof(box_vert_assembly) };
	boxpipe.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	boxpipe.VS = CD3DX12_SHADER_BYTECODE(box_vshader.get());
	boxpipe.PS = CD3DX12_SHADER_BYTECODE(box_pshader.get());
	boxpipe.SampleMask = UINT_MAX;
	boxpipe.SampleDesc = { 1,0 };
	boxpipe.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	boxpipe.NumRenderTargets = 1;
	boxpipe.pRootSignature = root_signature.get();
	boxpipe.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	boxpipe.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	boxpipe.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	boxpipe.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	boxpipe.BlendState.RenderTarget[0].BlendEnable = TRUE;
	boxpipe.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	boxpipe.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	boxpipe.DepthStencilState.DepthEnable = TRUE;
	boxpipe.DepthStencilState.StencilEnable = FALSE;
	boxpipe.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	com_ptr<ID3D12PipelineState> pipeline_state, graphic_pipeline, box_pipeline;
	if (FAILED(_device->CreateComputePipelineState(&compute_pipeline, IID_PPV_ARGS(&pipeline_state)))) 
	{
		LOG(ERROR) << "PSO creation failed.\n";
		return false;
	}
	if (FAILED(_device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&graphic_pipeline))))
	{
		LOG(ERROR) << "PSO creation failed (Graphic).\n";
		return false;
	}

	if (FAILED(_device->CreateGraphicsPipelineState(&boxpipe, IID_PPV_ARGS(&box_pipeline))))
	{
		LOG(ERROR) << "PSO creation failed (BOX).\n";
		return false;
	}

	const com_ptr<ID3D12GraphicsCommandList>& commands = _resource_commands.commands;
	const com_ptr<ID3D12Resource>& normal_table = _hva_resource.resources[0].get();
	const com_ptr<ID3D12Resource>& vert_buffer = _vertex_buffer.resources[0].get();
	const com_ptr<ID3D12Resource>& box_buffer = _vertex_buffer.resources[box_vert_buffer_idx].get();

	com_ptr<ID3D12Resource> normals_upload = _upload_buffers.resources[normal_upload_buffer_idx];
	com_ptr<ID3D12Resource> vert_upload = _upload_buffers.resources[vertex_upload_buffer_idx];
	com_ptr<ID3D12Resource> box_upload = _upload_buffers.resources[box_upload_buffer_idx];
	//D3D12_HEAP_PROPERTIES normals_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	//D3D12_RESOURCE_DESC normals_desc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(normal_table.get(), 0, 1));
	/*if (FAILED(_device->CreateCommittedResource(&normals_heap, D3D12_HEAP_FLAG_NONE,
		&normals_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&normals_upload))))
	{
		LOG(ERROR) << "NORMAL upload heap creation failed.\n";
		return false;
	}*/

	if (begin_command())
	{
		canvas_vertecies_data data = {};
		box_vertex_data box_data = {};
		D3D12_SUBRESOURCE_DATA subres = {}, vertres = {}, boxres = {};
		subres.pData = game_normals;
		subres.RowPitch = subres.SlicePitch = (sizeof game_normals);
		vertres.pData = &data;
		vertres.RowPitch = vertres.SlicePitch = (sizeof data);
		boxres.pData = &box_data;
		boxres.RowPitch = boxres.SlicePitch = (sizeof box_data);
		UpdateSubresources(commands.get(), normal_table.get(), normals_upload.get(), 0, 0, 1, &subres);
		UpdateSubresources(commands.get(), vert_buffer.get(), vert_upload.get(), 0, 0, 1, &vertres);
		UpdateSubresources(commands.get(), box_buffer.get(), box_upload.get(), 0, 0, 1, &boxres);
		transition_state(normal_table.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);
		transition_state(vert_buffer.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);
		transition_state(box_buffer.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);
		execute_commands();
		wait_for_completion();
	}

	_root_signature = root_signature;
	_pso = pipeline_state;
	_render_pso = graphic_pipeline;
	_box_pso = box_pipeline;
	return valid();
}

bool vpl_renderer::valid() const
{
	return _resource_commands.valid() && _vxl_resource.valid() && _general_fence.valid()
		&& _swapchain.valid() && _general_queue.get() && _pso.get() && _render_pso.get() && _box_pso.get() && _root_signature.get() &&
		_vertex_buffer.valid() && _upload_buffers.resources.size() >= 6 &&
		_resource_descriptor_heaps.get() && _render_target_views.get() &&
		_gui_descriptor_heaps.get();
}

bool vpl_renderer::begin_command()
{
	return _general_fence.raise_fence() && _resource_commands.reset();
}

bool vpl_renderer::wait_for_completion()
{
	return _general_fence.await_completion();
}

bool vpl_renderer::wait_for_sync()
{
	return _general_fence.raise_fence() && wait_for_completion();
}

void vpl_renderer::execute_commands()
{
	if (_resource_commands.close())
	{
		ID3D12CommandList* commands[] = { _resource_commands.commands.get() };
		_general_queue->ExecuteCommandLists(1, commands);
	}
}

bool vpl_renderer::transition_state(ID3D12Resource* resource, const D3D12_RESOURCE_STATES from, const D3D12_RESOURCE_STATES to)
{
	if (!resource || !_resource_commands.started) 
		return false;

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, from, to);
	_resource_commands.commands->ResourceBarrier(1, &barrier);
	return true;
}

bool vpl_renderer::render_loaded_vxl()
{
	if (!valid() || !vxl_resource_initiated())
		return false;

	const com_ptr<ID3D12GraphicsCommandList>& commands = _resource_commands.commands;
	const com_ptr<ID3D12Resource>& canvaz_resource = _vxl_resource.resources[0];
	const com_ptr<ID3D12Resource>& vpl_resource = _vpl_resource.resources[0];

	com_ptr<ID3D12Resource> temp_resource = _upload_buffers.resources[hva_constants_upload_buffer_idx];
	std::vector<vxl_cbuffer_data> final_data = _hva_buffer_storage;

	//create temp buffer
	//D3D12_HEAP_PROPERTIES upload_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	//D3D12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(_hva_resource.resources[1].get(), 0, 1));
	//if (FAILED(_device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
	//	nullptr, IID_PPV_ARGS(&temp_resource))))
	//{
	//	//report bugs
	//	return false;
	//}

	if (_renderer_resource_dirty)
	{
		bind_resource_table(-1);
		_renderer_resource_dirty = false;
	}

	const size_t buffer_idx = _swapchain.current_idx;
	const size_t rtv_increment = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE target = CD3DX12_CPU_DESCRIPTOR_HANDLE(_render_target_views->GetCPUDescriptorHandleForHeapStart(), buffer_idx, rtv_increment);
	D3D12_CPU_DESCRIPTOR_HANDLE depth = _depth_stencil_views->GetCPUDescriptorHandleForHeapStart();

	if (_box_rendered && begin_command())
	{
		transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commands->ClearRenderTargetView(target, _states.bgcolor.vector4_f32, 0, nullptr);
		commands->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		execute_commands();
		wait_for_completion();
	}

	for (size_t i = 0; i < final_data.size(); i++)
	{
		//preparing hva data
		vxl_cbuffer_data& data = final_data[i];
		//scale & rotation & translation
		DirectX::XMMATRIX translation_to_center = {}, scale = {}, offset = {}, base = data.vxl_transformation;
		DirectX::XMVECTOR scale_vec = (data.vxl_maxbound - data.vxl_minbound) / data.vxl_dimension;
		translation_to_center = DirectX::XMMatrixTranslationFromVector(data.vxl_minbound);
		scale = DirectX::XMMatrixScalingFromVector(scale_vec);
		offset = DirectX::XMMatrixTranslation(data.vxl_maxbound.vector4_f32[3], 0.0f, 0.0f);
		base.m[3][0] *= scale_vec.vector4_f32[0] * data.vxl_dimension.vector4_f32[3];
		base.m[3][1] *= scale_vec.vector4_f32[1] * data.vxl_dimension.vector4_f32[3];
		base.m[3][2] *= scale_vec.vector4_f32[2] * data.vxl_dimension.vector4_f32[3];
		data.vxl_transformation = translation_to_center * scale * base * offset * _states.world;
		data.remap_color = _states.remap_color;
		data.light_direction = _states.light_direction;
		
		//bind resources
		bind_resource_table(i);
		//start recording commands
		if (!begin_command())
			continue;

		const com_ptr<ID3D12Resource>& vxl_resource = _vxl_resource.resources[i + 1];
		const com_ptr<ID3D12Resource>& hva_resource = _hva_resource.resources[i + 1];

		//upload hva and light data
		D3D12_SUBRESOURCE_DATA subres = {};
		subres.pData = &data;
		subres.RowPitch = sizeof data;
		subres.SlicePitch = sizeof data;

		transition_state(hva_resource.get(), D3D12_RESOURCE_STATE_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		UpdateSubresources(commands.get(), hva_resource.get(), temp_resource.get(), 0, 0, 1, &subres);
		transition_state(hva_resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);
		
		ID3D12DescriptorHeap* heaps[] = { _resource_descriptor_heaps.get() };
		commands->SetDescriptorHeaps(_countof(heaps), heaps);
		transition_state(vxl_resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		transition_state(vpl_resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		uint32_t buffer_size = static_cast<uint32_t>(data.vxl_minbound.vector4_f32[3]);

		if (!_hardware_processing)
		{
			commands->SetComputeRootSignature(_root_signature.get());
			commands->SetComputeRootDescriptorTable(0, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			commands->SetComputeRootDescriptorTable(1, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			commands->SetComputeRootDescriptorTable(2, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());

			commands->SetPipelineState(_pso.get());
			commands->Dispatch(std::max(1u, (buffer_size + 63) / 64), 1, 1);
		}
		else
		{
			upload_scene_states state_constants = {};
			state_constants.data = _states;
			D3D12_SUBRESOURCE_DATA subres = {};
			subres.pData = &state_constants;
			subres.RowPitch = subres.SlicePitch = sizeof state_constants;
			transition_state(_pixel_const_buffer.resources[0].get(), D3D12_RESOURCE_STATE_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			UpdateSubresources(commands.get(), _pixel_const_buffer.resources[0].get(), _upload_buffers.resources[pixel_upload_buffer_idx].get(), 0, 0, 1, &subres);
			transition_state(_pixel_const_buffer.resources[0].get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADER_RESOURCE);
			transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			commands->OMSetRenderTargets(1, &target, false, &depth);

			commands->SetGraphicsRootSignature(_root_signature.get());
			commands->SetGraphicsRootDescriptorTable(0, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			commands->SetGraphicsRootDescriptorTable(1, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());
			commands->SetGraphicsRootDescriptorTable(2, _resource_descriptor_heaps->GetGPUDescriptorHandleForHeapStart());

			commands->SetPipelineState(_box_pso.get());

			//const float color_clear[] = { 0.0f,0.0f,1.0f,1.0f };
			//commands->ClearRenderTargetView(target, _states.bgcolor.vector4_f32, 0, nullptr);
			//commands->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

			D3D12_VIEWPORT viewport = { 0,0,width(),height(),0.0f,1.0f };
			D3D12_RECT scissor_rect = { 0,0,width(),height() };
			commands->RSSetViewports(1, &viewport);
			commands->RSSetScissorRects(1, &scissor_rect);

			D3D12_VERTEX_BUFFER_VIEW box_vert_view = {};
			box_vert_view.BufferLocation = _vertex_buffer.resources[box_vert_buffer_idx]->GetGPUVirtualAddress();
			box_vert_view.SizeInBytes = sizeof box_vertex_data;
			box_vert_view.StrideInBytes = sizeof box_vertex_data::face_vertex_data::_ld;

			//prepare input assembly
			commands->IASetVertexBuffers(0, 1, &box_vert_view);
			commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commands->DrawInstanced(box_vert_view.SizeInBytes / box_vert_view.StrideInBytes, buffer_size, 0, 0);

			transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			_box_rendered = true;
		}

		transition_state(vxl_resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
		transition_state(vpl_resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
		//execute commands & wait for completion
		execute_commands();
		wait_for_completion();
	}

	return true;
}

bool vpl_renderer::render_gui(const bool clear_target)
{
	if (!valid())
		return false;

	const com_ptr<ID3D12GraphicsCommandList>& commands = _resource_commands.commands;

	if (begin_command())
	{
		const size_t buffer_idx = _swapchain.current_idx;
		const size_t rtv_increment = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE target = CD3DX12_CPU_DESCRIPTOR_HANDLE(_render_target_views->GetCPUDescriptorHandleForHeapStart(), buffer_idx, rtv_increment);
		D3D12_CPU_DESCRIPTOR_HANDLE depth = _depth_stencil_views->GetCPUDescriptorHandleForHeapStart();

		ID3D12DescriptorHeap* heaps[] = { _gui_descriptor_heaps.get() };
		commands->SetDescriptorHeaps(_countof(heaps), heaps);

		transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		if (clear_target)
		{
			commands->ClearRenderTargetView(target, _states.bgcolor.vector4_f32, 0, nullptr);
			commands->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		}

		commands->OMSetRenderTargets(1, &target, false, &depth);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.get());

		transition_state(_swapchain.targets[buffer_idx].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		execute_commands();
		wait_for_completion();
	}

	return true;
}

bool vpl_renderer::clear_vxl_canvas()
{
	if (!valid())
		return false;
	
	const com_ptr<ID3D12Resource> clear_data = _upload_buffers.resources[clear_target_buffer_idx];
	const com_ptr<ID3D12Resource> canvas = _vxl_resource.resources[0];

	if (!clear_data || !canvas)
		return false;

	D3D12_TEXTURE_COPY_LOCATION src_location = {}, dst_location = {};
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src_location.pResource = clear_data.get();
	src_location.PlacedFootprint.Footprint.Width = width();
	src_location.PlacedFootprint.Footprint.Height = height();
	src_location.PlacedFootprint.Footprint.Depth = 1;
	src_location.PlacedFootprint.Footprint.Format = canvas_format;
	src_location.PlacedFootprint.Footprint.RowPitch = resource_pitch(width() * 8);

	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst_location.pResource = canvas.get();
	dst_location.SubresourceIndex = 0;

	if (begin_command())
	{
		transition_state(canvas.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
		_resource_commands.commands->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
		transition_state(canvas.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		execute_commands();
		return wait_for_completion();
	}

	return false;
}

std::vector<byte> vpl_renderer::front_buffer_data()
{
	if(!valid())
		return std::vector<byte>();

	const com_ptr<ID3D12Resource>& front = _vxl_resource.resources[0];
	const com_ptr<ID3D12GraphicsCommandList>& commands = _resource_commands.commands;
	const size_t imm_size = GetRequiredIntermediateSize(front.get(), 0, 1);
	const size_t data_pitch = width() * 8;
	const size_t download_pitch = resource_pitch(data_pitch);

	std::vector<byte> buffer(data_pitch * height());
	com_ptr<ID3D12Resource> read_back;
	D3D12_HEAP_PROPERTIES read_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC read_desc = CD3DX12_RESOURCE_DESC::Buffer(imm_size);

	if (FAILED(_device->CreateCommittedResource(&read_heap, D3D12_HEAP_FLAG_NONE,
		&read_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&read_back))))
	{
		LOG(ERROR) << "Failed to create read back resourec.\n";
		return std::vector<byte>();
	}

	if (begin_command())
	{
		transition_state(front.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

		D3D12_TEXTURE_COPY_LOCATION src_location = { front.get() };
		src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src_location.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION dst_location = { read_back.get() };
		dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst_location.PlacedFootprint.Footprint.Width = width();
		dst_location.PlacedFootprint.Footprint.Height = height();
		dst_location.PlacedFootprint.Footprint.Depth = 1;
		dst_location.PlacedFootprint.Footprint.Format = canvas_format;
		dst_location.PlacedFootprint.Footprint.RowPitch = download_pitch;

		commands->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
		transition_state(front.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		execute_commands();
		
		if (wait_for_completion())
		{
			byte* mapped_data = nullptr;
			if (SUCCEEDED(read_back->Map(0, nullptr, reinterpret_cast<void**>(&mapped_data))))
			{
				for (size_t i = 0; i < height(); i++)
					memcpy_s(&buffer[i * data_pitch], data_pitch, &mapped_data[i * download_pitch], data_pitch);
				read_back->Unmap(0, nullptr);
				return buffer;
			}
		}
	}
	return std::vector<byte>();
}

std::vector<byte> vpl_renderer::render_target_data()
{
	std::vector<byte> buffer;
	com_ptr<ID3D12Resource> result;
	if (!valid())
		return buffer;

	const com_ptr<ID3D12Resource> target = _swapchain.targets[_swapchain.current_idx];
	const com_ptr<ID3D12GraphicsCommandList>& commands = _resource_commands.commands;
	const size_t imm_size = GetRequiredIntermediateSize(target.get(), 0, 1);
	const size_t data_pitch = width() * 4;
	const size_t download_pitch = resource_pitch(data_pitch);

	D3D12_HEAP_PROPERTIES read_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC read_desc = CD3DX12_RESOURCE_DESC::Buffer(imm_size);

	if (FAILED(_device->CreateCommittedResource(&read_heap, D3D12_HEAP_FLAG_NONE,
		&read_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result))))
	{
		LOG(ERROR) << "Failed to create read back resource - download render target.\n";
		return buffer;
	}

	if (begin_command())
	{
		transition_state(target.get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);

		D3D12_TEXTURE_COPY_LOCATION src_location = { target.get() };
		src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		src_location.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION dst_location = { result.get() };
		dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst_location.PlacedFootprint.Footprint.Width = width();
		dst_location.PlacedFootprint.Footprint.Height = height();
		dst_location.PlacedFootprint.Footprint.Depth = 1;
		dst_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UINT;
		dst_location.PlacedFootprint.Footprint.RowPitch = download_pitch;

		commands->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
		transition_state(target.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
		execute_commands();

		if (wait_for_completion())
		{
			byte* mapped_data = nullptr;
			if (SUCCEEDED(result->Map(0, nullptr, reinterpret_cast<void**>(&mapped_data))))
			{
				buffer.resize(data_pitch * height());
				for (size_t i = 0; i < height(); i++)
					memcpy_s(&buffer[i * data_pitch], data_pitch, &mapped_data[i * download_pitch], data_pitch);
				result->Unmap(0, nullptr);
				return buffer;
			}
		}
	}

	return buffer;
}

void vpl_renderer::set_light_dir(const DirectX::XMVECTOR& dir)
{
	_states.light_direction = dir;
}

void vpl_renderer::set_scale_factor(const DirectX::XMVECTOR& scale)
{
	_states.scale = scale;
}

void vpl_renderer::set_world(const DirectX::XMMATRIX& world)
{
	_states.world = world;
}

void vpl_renderer::set_bg_color(const DirectX::XMVECTOR& color)
{
	_states.bgcolor = color;
}

void vpl_renderer::set_remap(const color& color)
{
	_states.remap_color = { static_cast<float>(color.r),static_cast<float>(color.g), static_cast<float>(color.b),1.0f };
}

void vpl_renderer::set_extra_light(const float extra)
{
	_states.canvas_dimension_extralight.vector4_f32[2] = extra;
}

bool vpl_renderer::hardware_processing() const
{
	return _hardware_processing;
}

size_t vpl_renderer::width() const
{
	return static_cast<size_t>(_states.canvas_dimension_extralight.vector4_f32[0]);
}

size_t vpl_renderer::height() const
{
	return static_cast<size_t>(_states.canvas_dimension_extralight.vector4_f32[1]);
}

bool vpl_renderer::resize_buffers()
{
	if(!valid())
		return false;

	DXGI_SWAP_CHAIN_DESC desc = {};
	_swapchain.swapchain->GetDesc(&desc);

	RECT client = {};
	GetClientRect(desc.OutputWindow, &client);

	//vxl canvas & depth_stencils
	size_t width = client.right - client.left, height = client.bottom - client.top;
	d3d12_resource_set canvas = {}, depth_stencils = {};

	/*
		!vxl_resources.add_empty_resource(device, canvas_format, D3D12_RESOURCE_DIMENSION_TEXTURE2D, width(), height(), 1) ||
		!depth_stencil_buffer.add_empty_resource(device,DXGI_FORMAT_D24_UNORM_S8_UINT,D3D12_RESOURCE_DIMENSION_TEXTURE2D, width(), height(),1,D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE)||
	*/
	if (!canvas.add_empty_resource(_device, canvas_format, D3D12_RESOURCE_DIMENSION_TEXTURE2D, width, height, 1) ||
		!depth_stencils.add_empty_resource(_device, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_RESOURCE_DIMENSION_TEXTURE2D, width, height, 1, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE))
		return false;

	const size_t clear_target_size = GetRequiredIntermediateSize(canvas.resources[0].get(), 0, 1);
	if (!canvas.add_empty_resource(_device, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_DIMENSION_BUFFER, clear_target_size, 1, 1,
		D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ))
		return false;
	
	float* upload_data = nullptr;
	if (FAILED(canvas.resources[1]->Map(0, nullptr, reinterpret_cast<void**>(&upload_data))))
	{
		return false;
	}

	for (size_t y = 0; y < height; y++)
	{
		for (size_t x = 0; x < width * 2; x++)
			upload_data[x] = 0.0f;
		upload_data += resource_pitch(width * 8) / sizeof(float);
	}

	canvas.resources[1]->Unmap(0, nullptr);

	wait_for_sync();

	//render targets
	_swapchain.targets.clear();
	if (FAILED(_swapchain.swapchain->ResizeBuffers(3u, 0u, 0u, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)))
		LOG(WARNING) << "Failed to resize buffers.\n";
	_swapchain.targets.resize(3);
	for (size_t i = 0; i < 3; i++)
		_swapchain.swapchain->GetBuffer(i, IID_PPV_ARGS(&_swapchain.targets[i]));

	_swapchain.current_idx = _swapchain.swapchain->GetCurrentBackBufferIndex();
	_swapchain.swapchain->GetDesc(&_swapchain.desc);

	_vxl_resource.resources[0] = canvas.resources[0];
	_upload_buffers.resources[clear_target_buffer_idx] = canvas.resources[1];
	_depth_stencil_resource.resources[0] = depth_stencils.resources[0];
	_states.canvas_dimension_extralight.vector4_f32[0] = static_cast<FLOAT>(width);
	_states.canvas_dimension_extralight.vector4_f32[1] = static_cast<FLOAT>(height);

	return _renderer_resource_dirty = true;
}

DirectX::XMVECTOR vpl_renderer::get_light_dir() const
{
	return _states.light_direction;
}

DirectX::XMMATRIX vpl_renderer::get_world() const
{
	return _states.world;
}

DirectX::XMVECTOR vpl_renderer::get_scale_factor() const
{
	return _states.scale;
}

DirectX::XMVECTOR vpl_renderer::get_bg_color() const
{
	return _states.bgcolor;
}

DirectX::XMVECTOR vpl_renderer::get_remap() const
{
	return _states.remap_color;
}

void d3d12_swapchain::discard()
{
	desc = { 0 };
	current_idx = { 0 };
	targets.clear();
	swapchain.reset();
	ref_device.reset();
}

bool d3d12_swapchain::valid() const
{
	return ref_device && swapchain && !targets.empty();
}

bool d3d12_swapchain::initialize(com_ptr<ID3D12CommandQueue> device_prox, com_ptr<IDXGIFactory6> factory, HWND output)
{
	if (valid())
		discard();

	if (!device_prox || !factory || output == NULL)
		return false;

	RECT window_rect = {};
	DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
	GetClientRect(output, &window_rect);
	swap_chain_desc.BufferCount = 3;
	swap_chain_desc.BufferDesc.Width = window_rect.right;
	swap_chain_desc.BufferDesc.Height = window_rect.bottom;
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.OutputWindow = output;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.Windowed = TRUE;
	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	com_ptr<IDXGISwapChain> temp_swapchain;
	com_ptr<IDXGISwapChain3> new_swapchain;
	if (FAILED(factory->CreateSwapChain(device_prox.get(), &swap_chain_desc, &temp_swapchain)) ||
		FAILED(temp_swapchain->QueryInterface(&new_swapchain)))
	{
		return false;
	}

	std::vector<com_ptr<ID3D12Resource>> buffers;
	buffers.resize(3);
	for (size_t i = 0; i < 3; i++)
	{
		if (FAILED(new_swapchain->GetBuffer(i, IID_PPV_ARGS(&buffers[i]))))
		{
			return false;
		}
	}

	ref_device = device_prox;
	targets = buffers;
	swapchain = new_swapchain;
	current_idx = swapchain->GetCurrentBackBufferIndex();
	desc = swap_chain_desc;

	return true;
}

canvas_vertecies_data::canvas_vertecies_data()
{
	vertex[0] = { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f };
	vertex[1] = { -1.0f, -3.0f, 0.0f, 0.0f, 2.0f };
	vertex[2] = { 3.0f,1.0f, 0.0f, 2.0f, 0.0f };
}

box_vertex_data::box_vertex_data()
{
	top =
	{
		{-0.5f,-0.5f,0.5f,1.0f},{-0.5f,0.5f,0.5f,1.0f},{0.5f,-0.5f,0.5f,1.0f},
		{-0.5f,0.5f,0.5f,1.0f},{0.5f,-0.5f,0.5f,1.0f},{0.5f,0.5f,0.5f,1.0f}
	};

	bottom =
	{
		{-0.5f,-0.5f,-0.5f,1.0f},{-0.5f,0.5f,-0.5f,1.0f},{0.5f,-0.5f,-0.5f,1.0f},
		{-0.5f,0.5f,-0.5f,1.0f},{0.5f,-0.5f,-0.5f,1.0f},{0.5f,0.5f,-0.5f,1.0f}
	};

	left =
	{
		{-0.5f,-0.5f,-0.5f,1.0f},{0.5f,-0.5f,-0.5f,1.0f},{-0.5f,-0.5f,0.5f,1.0f},
		{0.5f,-0.5f,-0.5f,1.0f},{-0.5f,-0.5f,0.5f,1.0f},{0.5f,-0.5f,0.5f,1.0f}
	};

	right =
	{
		{-0.5f,0.5f,-0.5f,1.0f},{0.5f,0.5f,-0.5f,1.0f},{-0.5f,0.5f,0.5f,1.0f},
		{0.5f,0.5f,-0.5f,1.0f},{-0.5f,0.5f,0.5f,1.0f},{0.5f,0.5f,0.5f,1.0f}
	};

	front =
	{
		{0.5f,-0.5f,-0.5f,1.0f},{0.5f,0.5f,-0.5f,1.0f},{0.5f,-0.5f,0.5f,1.0f},
		{0.5f,0.5f,-0.5f,1.0f},{0.5f,-0.5f,0.5f,1.0f},{0.5f,0.5f,0.5f,1.0f}
	};

	back =
	{
		{-0.5f,-0.5f,-0.5f,1.0f},{-0.5f,0.5f,-0.5f,1.0f},{-0.5f,-0.5f,0.5f,1.0f},
		{-0.5f,0.5f,-0.5f,1.0f},{-0.5f,-0.5f,0.5f,1.0f},{-0.5f,0.5f,0.5f,1.0f}
	};
}
