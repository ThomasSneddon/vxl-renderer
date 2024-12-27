#include "d3d.h"
#include "gdi.h"
#include "hva.h"
#include "vxl.h"
#include "vpl.h"
#include "config.h"

#include "stb_includer.h"
#include "imgui.h"
#include "imGuIZMO.quat/imGuIZMO.quat/imGuIZMOquat.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include <CommCtrl.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <chrono>
#include <format>

#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "stb.lib")
#pragma comment(lib, "imgui.lib")

static const float pixels_to_leptons = 30.0f * sqrt(2.0f) / 256.0f;

struct colorset_desc
{
	std::string name = "Default";
	size_t start = 1, end = 255;
	bool self_restricted = false;
	std::vector<bool> color_selection = std::vector<bool>(256u, true);
	float ambient = 0.6f, diffuse = 0.8f, specular = 1.2f;

	colorset_desc() {
		color_selection[0] = false;
	}

	void parse_ini(const std::string& keyname, const config::value_type& values)
	{
		name = keyname;
		size_t remained_paras = values.size();

		//start
		if (remained_paras > 0u) {
			start = atoi(values[0].c_str());
			start = std::clamp(start, (size_t)1u, (size_t)255u);
			remained_paras--;
		}

		//end
		if (remained_paras > 0u) {
			end = atoi(values[1].c_str());
			end = std::clamp(end, (size_t)1u, (size_t)255u);
			remained_paras--;
		}

		if (end <= start)
			std::swap(end, start);

		//self restricted
		if (remained_paras > 0u) {
			std::fill_n(color_selection.begin(), color_selection.size(), false);

			char first = toupper(values[2][0]);
			if (first == 'Y' || first == '1' || first == 'T')
				std::fill_n(color_selection.begin() + start, end - start + 1u, true);
			else
				std::fill_n(color_selection.begin() + 1u, 255u, true);

			remained_paras--;
		}

		//ambient
		if (remained_paras > 0u) {
			ambient = atof(values[3].c_str());
			remained_paras--;
		}

		//diffuse
		if (remained_paras > 0u) {
			diffuse = atof(values[4].c_str());
			remained_paras--;
		}

		//specular
		if (remained_paras > 0u) {
			specular = atof(values[5].c_str());
			remained_paras--;
		}

		//color selection
		size_t starting_idx = 6u, para_idx = 0u;
		if (remained_paras > 0u) {
			std::fill_n(color_selection.begin(), color_selection.size(), false);
		}

		while (remained_paras > 0u) {
			const auto& value = values[starting_idx + para_idx];
			if (value != "0")
				color_selection[para_idx] = true;

			remained_paras--;
			para_idx++;
		}
		color_selection[0] = false;
	}
};
namespace mainproc
{
	vpl_renderer renderer;
	HWND mainwin = NULL;
};

namespace shot
{
	size_t directions = 8;
	std::string output_dir = "output";
	std::string filename;
	bool generate_ingame_like_previews = false;
	bool generate_shadow = false;
	bool generate_integrated_shadow = false;
	std::string bgfilename = "background.png";
	size_t celloffsetx = 6;
	size_t celloffsety = 6;
}

namespace assets
{
	::vpl vpl;
	::hva hva, tur_hva, barl_hva;
	::vxl vxl, tur_vxl, barl_vxl;
	::palette pal;
	::config ini;

	std::filesystem::path vxl_path, hva_path, tur_path, tur_hvapath, barl_path, barl_hvapath;
}

namespace ui_states
{
	float rotation_theta = 0.0f, rotation_phi = 0.0f;
	float remap[3] = { 1.0f,0.0f,0.0f };

	float turret_offset = 0.0f;
	float turret_rotation = 0.0f;

	bool playing = false;
	size_t current_frame = 0;

	//drag stuff
	float xy_angle = 0.0f, z_angle = 0.0f;
	float scale = 1.0f;

	//DirectX::XMVECTOR light_direction = { 0.2013022f,-0.9101138f,-0.3621709f,0.0f };
	vgm::Vec3 light_direction_config = { 0.2013022f,-0.9101138f,-0.3621709f };
	vgm::Vec3 light_direction = light_direction_config;
	bool direction_panel_locked = false;
	//float ambient_diffuse_specular[3] = { 0.6f,0.8f,1.2f };
	//float ambient = 0.6f, diffuse = 0.8f, specular = 1.2f;
	bool reset_checked = false;

	std::vector<colorset_desc> color_sets = { colorset_desc() };
	size_t color_set_idx = 0u;
	bool show_colorsel_panel = false;

	float extra_light = 0.2f;
	
	void reset_values()
	{
		rotation_theta = rotation_phi = turret_rotation = xy_angle = z_angle = 0.0f;
		light_direction = light_direction_config;// { 0.2013022f, -0.9101138f, -0.3621709f };
		scale = 1.0f;
	}
}


std::filesystem::path get_exe_path()
{
	std::wstring path_buffer;
	path_buffer.resize(65536);

	GetModuleFileName(NULL, path_buffer.data(), 65536);
	return std::filesystem::path(path_buffer).remove_filename();
}

void screen_shot(const std::string& filename, const std::string& path)
{
	auto& renderer = mainproc::renderer;

	if (!renderer.valid())
		return;

	size_t directions = shot::directions;
	float starting_angle = -0.75f * DirectX::g_XMPi.f[0];
	float angle_step = DirectX::g_XMTwoPi.f[0] / directions;

	const size_t body_frames = assets::hva.frame_count();
	const size_t turret_frames = assets::tur_hva.frame_count();
	const size_t barrel_frames = assets::barl_hva.frame_count();

	size_t min_ab = 1, min_bc = 1;
	for (size_t i = 1; i <= std::min(body_frames, turret_frames); i++)
		if (!(body_frames % i) && !(turret_frames % i))
			min_ab = i;

	size_t max_ab = std::max(body_frames, static_cast<size_t>(1u)) * std::max(turret_frames, static_cast<size_t>(1u)) / min_ab;
	for (size_t i = 1; i <= std::min(barrel_frames, max_ab); i++)
		if (!(barrel_frames % i) && !(max_ab % i))
			min_bc = i;

	if (!std::filesystem::exists(path))
		std::filesystem::create_directory(path);

	std::filesystem::path target(path);
	target /= filename;

	const float reload_Z = static_cast<float>(ui_states::turret_rotation) * DirectX::g_XMTwoPi.f[0] / 100.0f;
	const hva* hvas[] = { &assets::hva,&assets::tur_hva,&assets::barl_hva };
	size_t frame_per_direction = std::max(max_ab, static_cast<size_t>(1u)) * std::max(barrel_frames, static_cast<size_t>(1u)) / min_bc;
	for (size_t current_dir = 0u, current_file_idx = 0u; current_dir < directions; current_dir++)
	{
		float current_angle = starting_angle + current_dir * angle_step;
		DirectX::XMMATRIX world = DirectX::XMMatrixRotationZ(current_angle);
		for (size_t frame_idx = 0u; frame_idx < frame_per_direction; frame_idx++)
		{
			const size_t frames[] = { frame_idx,frame_idx,frame_idx };
			const float rotations[] = { current_angle,current_angle + reload_Z,current_angle + reload_Z };
			const float offsets[] = { 0.0f, ui_states::turret_offset,ui_states::turret_offset };

			renderer.reload_hva(hvas, frames, rotations, offsets, _countof(hvas));
			//renderer.set_world(world);
			renderer.clear_vxl_canvas();
			renderer.render_loaded_vxl();
			auto front_buffer = renderer.render_target_data();
			if (!front_buffer.empty())
			{
				target.replace_filename(filename + " " + std::to_string(current_file_idx++));
				target.replace_extension(".PNG");
				stbi_write_png(target.string().c_str(), 256, 256, 4, front_buffer.data(), 0);
			}
		}
	}

	if (shot::generate_ingame_like_previews)
	{
		constexpr const size_t bgchannels = 4u;
		constexpr const size_t cell_width = 60u;
		constexpr const size_t cell_height = 30u;

		const size_t bgwidth = 256u + cell_width * shot::celloffsetx * 2u;
		const size_t bgheight = 256u + cell_height * shot::celloffsety * 2u;
		std::unique_ptr<byte> output_buffer(new byte[bgwidth * bgheight * bgchannels]);
		const size_t outputbuffer_pitch = bgwidth * bgchannels;

		directions = 8u;
		angle_step = DirectX::g_XMTwoPi.f[0] / directions;
		starting_angle = DirectX::g_XMNegativePi.f[0]; //lefttop

		auto tempworld = renderer.get_world();
		auto tempscale = renderer.get_scale_factor();
		auto tempbgcolor = renderer.get_bg_color();

		const size_t frames[] = { 0,0,0 };
		const float rotations[] = { 0.0f,0.0f,0.0f };
		const float offsets[] = { 0.0f, ui_states::turret_offset,ui_states::turret_offset };

		renderer.reload_hva(hvas, frames, rotations, offsets, _countof(hvas));
		renderer.set_scale_factor({ 1.0f,1.0f,1.0f,1.0f });
		renderer.set_bg_color({ 0.0f,0.0f,1.0f,0.0f });

		std::vector<std::vector<byte>> render_result_storage;
		std::vector<std::vector<byte>> shadow_result_storage;
		auto rotation_matrix = DirectX::XMMatrixIdentity();
		auto shadow_matrix = DirectX::XMMatrixScaling(1.0f, 1.0f, 0.0f);
		for (size_t i = 0; i < directions; i++)
		{
			rotation_matrix = DirectX::XMMatrixRotationZ(starting_angle + angle_step * i);
			renderer.set_world(rotation_matrix);
			renderer.clear_vxl_canvas();
			renderer.render_loaded_vxl();
			render_result_storage.push_back(renderer.render_target_data());
			renderer.set_world(rotation_matrix * shadow_matrix);
			renderer.clear_vxl_canvas();
			renderer.render_loaded_vxl();
			shadow_result_storage.push_back(renderer.render_target_data());

		}

		renderer.set_world(tempworld);
		renderer.set_scale_factor(tempscale);
		renderer.set_bg_color(tempbgcolor);

		const auto check_function = [](const std::vector<byte>& target_data)->bool {
			return target_data.empty();
		};

		auto invalid = std::find_if(render_result_storage.begin(), render_result_storage.end(), check_function);
		auto invalid_shadow = std::find_if(shadow_result_storage.begin(), shadow_result_storage.end(), check_function);
		//write render result to target
		if (invalid == render_result_storage.end() && invalid_shadow == shadow_result_storage.end() && output_buffer && render_result_storage.size() == directions && shadow_result_storage.size() == directions)//valid
		{
			std::filesystem::path bgfile = get_exe_path() / shot::bgfilename;
			auto filebuffer = read_whole_file(bgfile.string());

			//clear image
			memset(output_buffer.get(), 0, bgwidth * bgheight * bgchannels);
			//write bg image
			if (filebuffer.get())
			{
				int output_width = 0, output_height = 0, output_channels = 0;
				auto filesize = std::filesystem::file_size(bgfile);
				const auto bgimage_data = stbi_load_from_memory(reinterpret_cast<byte*>(filebuffer.get()), filesize, &output_width, &output_height, &output_channels, STBI_rgb_alpha);

				//blit the image to the center
				if (bgimage_data)
				{
					const POINT image_pos = { ((int)bgwidth - (int)output_width) / 2,((int)bgheight - (int)output_height) / 2 };
					RECT surface_rect = { 0,0,bgwidth,bgheight };
					RECT image_rect = { image_pos.x,image_pos.y,image_pos.x + output_width,image_pos.y + output_height };
					RECT intersected = {};
					IntersectRect(&intersected, &image_rect, &surface_rect);

					const size_t image_startx = intersected.left - image_pos.x;
					const size_t image_starty = intersected.top - image_pos.y;
					const size_t surface_startx = intersected.left;
					const size_t surface_starty = intersected.top;
					const size_t blit_width = intersected.right - intersected.left;
					const size_t blit_height = intersected.bottom - intersected.top;
					const size_t src_pitch = output_width * 4u;

					for (size_t y = 0; y < blit_height; y++)
					{
						for (size_t x = 0; x < blit_width; x++)
						{
							size_t surface_cur = (surface_starty + y) * outputbuffer_pitch + (surface_startx + x) * bgchannels;
							size_t image_buff_cur = (image_starty + y) * src_pitch + (image_startx + x) * 4u;

							*reinterpret_cast<RGBQUAD*>(&output_buffer.get()[surface_cur]) = *reinterpret_cast<RGBQUAD*>(&bgimage_data[image_buff_cur]);
						}
					}
				}

				if (bgimage_data)
					stbi_image_free(bgimage_data);
			}

			//write render result
			static const size_t index_to_direction[] = { 1u,0u,2u,7u,3u,5u,6u,4u };
			static const size_t direction_to_block[] = { 0u,1u,2u,5u,8u,7u,6u,3u };
			for (size_t index = 0; index < directions; index++)
			{
				const size_t dir = index_to_direction[index];
				const size_t block_idx = direction_to_block[dir];

				const size_t block_y = block_idx / 3;
				const size_t block_x = block_idx - block_y * 3;

				const size_t start_x = block_x * shot::celloffsetx * cell_width;
				const size_t start_y = block_y * shot::celloffsety * cell_height;

				auto& result = render_result_storage[dir];
				auto& shadow = shadow_result_storage[dir];
				const size_t src_pitch = 256u * 4u;
				for (size_t y = 0; y < 256u; y++)
				{
					for (size_t x = 0; x < 256u; x++)
					{
						size_t src_location = y * src_pitch + x * 4u;
						size_t dst_location = (start_y + y) * outputbuffer_pitch + (start_x + x) * bgchannels;

						if (shadow[src_location + 3u])
						{
							auto& color = *reinterpret_cast<RGBQUAD*>(&output_buffer.get()[dst_location]);
							if (output_buffer.get()[dst_location + 3u])
							{
								color.rgbRed >>= 1u;
								color.rgbGreen >>= 1u;
								color.rgbBlue >>= 1u;
							}
							else
							{
								color = { 0u,0u,0u,127u };
							}
						}

						if (result[src_location + 3u])
						{
							*reinterpret_cast<RGBQUAD*>(&output_buffer.get()[dst_location]) = *reinterpret_cast<RGBQUAD*>(&result[src_location]);
						}
					}
				}

			}

			target.replace_filename("Preview");
			target.replace_extension("png");
			stbi_write_png(target.string().c_str(), bgwidth, bgheight, bgchannels, output_buffer.get(), 0);
		}
	}
}

std::filesystem::path select_folder()
{
	std::filesystem::path result;

	com_ptr<IFileSaveDialog> dialog;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog))))
	{
		COMDLG_FILTERSPEC allowed_types[] = { {TEXT("VPL"),TEXT("*.vpl")} };
		dialog->SetFileTypes(_countof(allowed_types), allowed_types);
		dialog->SetFileTypeIndex(1u);

		if (SUCCEEDED(dialog->Show(NULL)))
		{
			com_ptr<IShellItem> items;
			if (SUCCEEDED(dialog->GetResult(&items)))
			{
				LPWSTR pathres = nullptr;
				items->GetDisplayName(SIGDN_FILESYSPATH, &pathres);

				if (pathres)
				{
					result = pathres;

					if (!result.has_extension())
						result.replace_extension(L".vpl");

					CoTaskMemFree(pathres);
					pathres = nullptr;
				}
			}
		}
	}
	return result;
}

/*
double ColourDistance(RGB e1, RGB e2)
{
  long rmean = ( (long)e1.r + (long)e2.r ) / 2;
  long r = (long)e1.r - (long)e2.r;
  long g = (long)e1.g - (long)e2.g;
  long b = (long)e1.b - (long)e2.b;
  return sqrt((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}
*/
void update_vpl()
{
	auto& vpl = assets::vpl;
	auto& pal = assets::pal;
	const auto& colorset = ui_states::color_sets[ui_states::color_set_idx];

	auto vpl_curve = [&colorset](const size_t section_idx)->double {
		double f = section_idx / 16.0;

		double spec = colorset.specular;
		double ambient = colorset.ambient;
		double diffuse = colorset.diffuse;

		return f < 1.0 ? (ambient + f * diffuse) : (ambient + diffuse + (f - 1.0) * (diffuse + spec));
	};

	auto color_sqdistance = [](const color& color1, const color& color2) {
		int dr = (int)color1.r - (int)color2.r;
		int dg = (int)color1.g - (int)color2.g;
		int db = (int)color1.b - (int)color2.b;
		
		int rmean = ((int)color1.r + (int)color2.r) / 2;
		return (((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rmean) * db * db) >> 8);
	};

	for (size_t section_i = 0u; section_i < 32u; section_i++)
	{
		for (size_t i = colorset.start; i < colorset.end; i++)
		{
			double effector = vpl_curve(section_i);
			const auto& orig_color = pal.entry()[i];
			color target_color =
			{
				static_cast<byte>(std::clamp(orig_color.r * effector,0.0,255.0)),
				static_cast<byte>(std::clamp(orig_color.g * effector,0.0,255.0)),
				static_cast<byte>(std::clamp(orig_color.b * effector,0.0,255.0)),
			};

			double nearest_dis = 3.0 * 255.0 * 255.0;
			size_t nearest_i = 1u;

			size_t findstart = /*colorset.self_restricted ? colorset.start : */1u;
			size_t findend = /*colorset.self_restricted ? colorset.end : */255u;
			for (size_t f = findstart; f <= findend; f++)
			{
				if (!colorset.color_selection[f])
					continue;

				double dis = color_sqdistance(pal.entry()[f], target_color);
				if (dis < nearest_dis)
				{
					nearest_dis = dis;
					nearest_i = f;
				}
			}

			vpl.data()[section_i][i] = nearest_i;
		}
	}
}

std::filesystem::path value_cache_path()
{
	return get_exe_path() / "valuecache.ini";;
}

void save_vpl_setting_cache()
{
	const auto& path = value_cache_path();
	const auto& mainsec_file = assets::vxl_path.filename();

	//下次再说
	wchar_t printbuffer[0x100] = { 0 };
	for (const auto& set : ui_states::color_sets)
	{
		swprintf_s(printbuffer, L"%hs", set.name.c_str());
		auto values = std::format(L"{},{},{},{},{},{}", set.start, set.end, set.self_restricted, set.ambient, set.diffuse, set.specular);
		for (const auto sel : set.color_selection) 
		{
			values += L',' + std::to_wstring(sel);
		}

		WritePrivateProfileStringW(mainsec_file.c_str(), printbuffer, values.c_str(), path.c_str());
	}
}

void load_vpl_setting_cache()
{
	const auto& path = value_cache_path();
	config cache(path.string());

	if (!cache.is_loaded() || ui_states::color_sets.size() == 1u)
		return;

	const auto& mainsec_file = assets::vxl_path.filename().string();
	const auto& section = cache.section(mainsec_file);
	if (section.empty())
		return;

	auto desc_match = [](const colorset_desc& l, const colorset_desc& r)->bool {
		return l.name == r.name && l.start == r.start && l.end == r.end;//&& l.self_restricted == r.self_restricted;
	};

	for (const auto& pairs : section)
	{
		colorset_desc cache_desc = {};

		cache_desc.parse_ini(pairs.first, pairs.second);
		for (size_t i = 1u; i < ui_states::color_sets.size(); i++) 
		{
			auto& set = ui_states::color_sets[i];
			if (desc_match(cache_desc, set))
			{
				set.ambient = cache_desc.ambient;
				set.diffuse = cache_desc.diffuse;
				set.specular = cache_desc.specular;
				set.color_selection = cache_desc.color_selection;
			}

			ui_states::color_set_idx = i;
			update_vpl();
		}

		ui_states::color_set_idx = 0;
	}
}

bool load_settings()
{
	auto& config = assets::ini;

	if (!config.is_loaded())
		return false;

	const auto settings = "Settings";
	const auto colorsets = "ColorSets";

	shot::output_dir = config.read_string(settings, "ScreenshotOutputDir", shot::output_dir);
	shot::directions = config.read_int(settings, "DirectionCount", shot::directions);
	shot::bgfilename = config.read_string(settings, "BackgroundFileName", shot::bgfilename);
	shot::celloffsetx = static_cast<size_t>(std::abs(config.read_int(settings, "CellOffsetX", shot::celloffsetx)));
	shot::celloffsety = static_cast<size_t>(std::abs(config.read_int(settings, "CellOffsetY", shot::celloffsety)));

	DirectX::XMVECTOR setting_color = { 0.0f,0.0f,1.0f,1.0f };
	auto bgcolor = assets::ini.value_as_int(settings, "BackgroundColor");
	if (bgcolor.size() >= 4)
	{
		for (size_t i = 0; i < 4; i++)
			setting_color.vector4_f32[i] = static_cast<float>(std::clamp(bgcolor[i], 0, 255)) / 255.0f;
		mainproc::renderer.set_bg_color(setting_color);
	}

	shot::generate_ingame_like_previews = assets::ini.read_bool(settings, "GenerateIngameViews", shot::generate_ingame_like_previews);
	shot::generate_shadow = assets::ini.read_bool(settings, "GenerateShadow", shot::generate_shadow);
	shot::generate_integrated_shadow = assets::ini.read_bool(settings, "IntegratedShadow", shot::generate_integrated_shadow);

	const auto def_light_data = assets::ini.value_as_double(settings, "DefaultLightDir");
	if (def_light_data.size() >= 3u) 
	{
		ui_states::light_direction_config = { (float)def_light_data[0],(float)def_light_data[1],(float)def_light_data[2] };
		ui_states::light_direction = ui_states::light_direction_config;
	}

	//colorsets
	const auto& colorset_sec = config.section(colorsets);
	for (const auto& keypairs : colorset_sec)
	{
		colorset_desc desc = {};

		desc.parse_ini(keypairs.first, keypairs.second);

		ui_states::color_sets.push_back(desc);
	}

	load_vpl_setting_cache();
	return true;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT __stdcall winproc(HWND window, UINT msg, WPARAM wpara, LPARAM lpara)
{
	if (ImGui_ImplWin32_WndProcHandler(window, msg, wpara, lpara))
		return true;

	auto& io = ImGui::GetIO();
	switch (msg)
	{
	case WM_SIZE:
		mainproc::renderer.resize_buffers();

		return 0;
	case WM_SYSCOMMAND:
		if ((wpara & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;

	case WM_MOUSEMOVE:
	{
		static bool track_started = false;
		static POINT last_position = { 0,0 };

		if (io.WantCaptureMouse)
			break;

		if (wpara & MK_LBUTTON)
		{
			if (!track_started)
			{
				track_started = true;
				last_position = { LOWORD(lpara),HIWORD(lpara) };
				SetCapture(window);
			}
			else
			{
				auto& zangle = ui_states::z_angle;
				auto& xyangle = ui_states::xy_angle;

				zangle -= (GET_X_LPARAM(lpara) - last_position.x) * DirectX::g_XMPi.f[0] / 80.0f;
				xyangle -= (GET_Y_LPARAM(lpara) - last_position.y) * DirectX::g_XMTwoPi.f[0] / 160.0f;

				if (std::abs(zangle) > DirectX::g_XMTwoPi.f[0])
					zangle += -zangle / std::abs(zangle) * DirectX::g_XMTwoPi.f[0];
				if (std::abs(xyangle) > DirectX::g_XMTwoPi.f[0])
					xyangle += -xyangle / std::abs(xyangle) * DirectX::g_XMTwoPi.f[0];

				last_position = { GET_X_LPARAM(lpara),GET_Y_LPARAM(lpara) };
			}
		}
		else
		{
			if (track_started)
			{
				track_started = false;
				last_position = { 0,0 };
				ReleaseCapture();
			}
		}
		
		return 0;
	}
	case WM_MOUSEWHEEL:
	{
		if (io.WantCaptureMouse)
			break;

		auto& scale = ui_states::scale;
		float delta = GET_WHEEL_DELTA_WPARAM(wpara) / WHEEL_DELTA * 0.25f;
		scale += delta;
		scale = std::max(scale, 0.2f);
		return 0;
	}
	case WM_COMMAND:
	{
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(window, msg, wpara, lpara);
}

bool SelectionRect(ImVec2& start_pos, ImVec2& end_pos, ImGuiMouseButton mouse_button = ImGuiMouseButton_Left)
{
	//if (!ImGui::GetIO().WantCaptureMouse)
	//	return false;

	if (ImGui::IsMouseClicked(mouse_button))
		start_pos = ImGui::GetMousePos();

	if (ImGui::IsMouseDown(mouse_button))
	{
		end_pos = ImGui::GetMousePos();
		ImDrawList* draw_list = ImGui::GetForegroundDrawList(); //ImGui::GetWindowDrawList();

		draw_list->AddRect(start_pos, end_pos, ImGui::GetColorU32(IM_COL32(0, 130, 216, 255)));   // Border
		draw_list->AddRectFilled(start_pos, end_pos, ImGui::GetColorU32(IM_COL32(0, 130, 216, 50)));    // Background
	}

	return ImGui::IsMouseReleased(mouse_button);
}

void PaletteSelectionPanel(const std::string& label, const palette& pal, std::vector<bool>& idx_status) 
{
	//
	static constexpr size_t grid_width = 16u, grid_height = 8u, expansion_dis = 0u;
	static constexpr ImVec2 child_size = ImVec2(grid_width * 8u + expansion_dis * 2u, grid_height * 32u + expansion_dis * 2u);
	static constexpr ImVec2 button_size = ImVec2(grid_width, grid_height);

	if (idx_status.size() != 256u)
		return;

	static bool clearmode = false;
	ImGui::Checkbox("Clear Mode", &clearmode);

	const auto& style = ImGui::GetStyle();
	const float titlebar_height = style.FramePadding.x * 2.0f + ImGui::GetFontSize() - 2.0f;
	ImGui::SetNextWindowBgAlpha(0.5f);
	ImGui::SetNextWindowSize(child_size + ImVec2(0.0f, titlebar_height));

	ImGui::Begin(label.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
	auto draw_list = ImGui::GetWindowDrawList();

	ImGui::SetCursorPos({ 0.0f,0.0f });
	const auto lefttop = ImGui::GetCursorScreenPos() + ImVec2(0.0f, titlebar_height);
	const auto colors = pal.entry();

	static ImVec2 start = {}, end = {};

	if (SelectionRect(start, end) && start != end && ui_states::color_set_idx != 0u) // select and mouse released 
	{
		ImVec2 lt = { std::min(start.x,end.x),std::min(start.y,end.y) };
		ImVec2 rb = { std::max(start.x,end.x),std::max(start.y,end.y) };

		lt -= lefttop;
		rb -= lefttop;

		lt.x = std::clamp(lt.x, 0.0f, child_size.x);
		lt.y = std::clamp(lt.y, 0.0f, child_size.y);
		rb.x = std::clamp(rb.x, 0.0f, child_size.x);
		rb.y = std::clamp(rb.y, 0.0f, child_size.y);

		size_t start_col = static_cast<size_t>(std::clamp(lt.x / grid_width, 0.0f, 7.5f)),
			end_col = static_cast<size_t>(std::clamp(rb.x / grid_width, 0.0f, 7.5f)),
			start_row = static_cast<size_t>(std::clamp(lt.y / grid_height, 0.0f, 31.5f)),
			end_row = static_cast<size_t>(std::clamp(rb.y / grid_height, 0.0f, 31.5f));

		for (size_t c = start_col; c <= end_col; c++)
		{
			for (size_t r = start_row; r <= end_row; r++)
			{
				size_t i = c * 32u + r;
				idx_status[i] = !clearmode;
			}
		}

		idx_status[0] = false;
	}

	for (size_t i = 0u; i < 256u; i++)
	{
		const auto& palcol = colors[i];
		const ImColor col = ImColor((int)palcol.r, palcol.g, palcol.b);
		const size_t column = i / 32u;
		const size_t row = i - column * 32u;

		const ImVec2 pos = lefttop + ImVec2(column * grid_width + expansion_dis, row * grid_height + expansion_dis);
		draw_list->AddRectFilled(pos, pos + button_size, col);

		ImGui::SetCursorScreenPos(pos);
		
		//Default set is not editable, but button should be used
		if (ImGui::InvisibleButton(std::format("Color##{}", i).c_str(), button_size) && ui_states::color_set_idx != 0u)
		{
			//sha bi std::vector<bool>
			idx_status[i] = !idx_status[i];
		}

		if (idx_status[i])
		{
			ImColor tickcol = ImColor(255 - palcol.r, 255 - palcol.g, 255 - palcol.b);
			const ImVec2 lb = pos + ImVec2(0.0f, grid_height), rt = pos + ImVec2(grid_width, 0.0f);
			
			draw_list->AddLine(pos, pos + button_size, tickcol);
			draw_list->AddLine(lb, rt, tickcol);
		}
	}

	ImGui::End();
}

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmdline, int cmdshow)
{
#if _DEBUG
	init_console();
#endif
	UNREFERENCED_PARAMETER(CoInitialize(nullptr));
	logger::initialize();

	std::filesystem::path current_dir = get_exe_path();
	assets::vpl.load((current_dir / "voxels.vpl").string());
	assets::pal.load((current_dir / "unittem.pal").string());
	assets::ini.load((current_dir / "settings.ini").string());
	std::string gui_config = (get_exe_path() / "imgui.ini").string();
	std::string gui_log = (get_exe_path() / "imgui.log").string();
	//std::filesystem::path gui_log = get_exe_path() / "imgui_log.ini";

	//generate_normal_table_codes("D:\\Yuris Revenge\\gamemd.exe");
#ifdef _DEBUG
	std::string cmd_temp = "C:\\Users\\gual0\\Desktop\\assets\\boa\\boa.vxl";
#else
	std::string cmd_temp(cmdline);
#endif
	if (*cmd_temp.begin() == '\"')
		cmd_temp.erase(cmd_temp.begin());
	if (*(cmd_temp.end() - 1) == '\"')
		cmd_temp.erase(cmd_temp.end() - 1);

	if (!std::filesystem::exists(cmd_temp))
	{
		LOG(ERROR) << "VXL file not found.\n";
		logger::uninitialize();
		return 0;
	}

	std::filesystem::path filepath(cmd_temp);
	std::string base_filename = filepath.filename().replace_extension().string();
	shot::filename = base_filename;
	assets::vxl.load(filepath.string());
	assets::vxl_path = filepath;
	filepath.replace_extension("hva");
	assets::hva.load(filepath.string());
	assets::hva_path = filepath;

	filepath.replace_filename(base_filename + "tur");
	filepath.replace_extension("vxl");
	assets::tur_vxl.load(filepath.string());
	assets::tur_path = filepath;
	filepath.replace_extension("hva");
	assets::tur_hva.load(filepath.string());
	assets::tur_hvapath = filepath;

	filepath.replace_filename(base_filename + "barl");
	filepath.replace_extension("vxl");
	assets::barl_vxl.load(filepath.string());
	assets::barl_path = filepath;
	filepath.replace_extension("hva");
	assets::barl_hva.load(filepath.string());
	assets::barl_hvapath = filepath;

	load_settings();

	const TCHAR* classname = TEXT("CXC_115");

	WNDCLASSEX wndclass = {};
	wndclass.cbSize = sizeof wndclass;
	wndclass.lpszClassName = classname;
	wndclass.lpfnWndProc = winproc;
	wndclass.hInstance = instance;
	//wndclass.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
	wndclass.style = CS_CLASSDC;
	RegisterClassEx(&wndclass);

	HWND mainwin = CreateWindow(classname, TEXT("VXL Utilities"), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 256, 256, NULL, NULL, instance, NULL);
	if (mainwin)
	{
		mainproc::mainwin = mainwin;
		UpdateWindow(mainwin);
		
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.IniFilename = gui_config.c_str();
		io.LogFilename = gui_log.c_str();
		
		ImGui::StyleColorsDark();
		if (ImGui_ImplWin32_Init(mainwin) && mainproc::renderer.initialize(mainwin))
		{
			MoveWindow(mainwin, 100, 100, 1000, 600, TRUE);

			mainproc::renderer.load_pal(assets::pal);
			mainproc::renderer.load_vpl(assets::vpl);
			mainproc::renderer.load_vxl(assets::vxl, assets::hva, 0, true);
			mainproc::renderer.load_vxl(assets::tur_vxl, assets::tur_hva, 0);
			mainproc::renderer.load_vxl(assets::barl_vxl, assets::barl_hva, 0);

			auto last_tick = std::chrono::high_resolution_clock::now();
			bool shutdown = false;
			while (!shutdown)
			{
				MSG message = {};
				while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&message);
					DispatchMessage(&message);

					if (message.message == WM_QUIT)
						shutdown = true;
				}

				if (shutdown)
					break;

				ImGui_ImplDX12_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();

				ImGui::SetNextWindowBgAlpha(0.75f);
				//ImGui::SetNextWindowPos({ 5.0f,5.0f });
				//ImGui::SetNextWindowSize({ 350.0f,350.0f });
				{
					ImGui::Begin("Control Panel");                          // Create a window called "Hello, world!" and append into it.
					//ImGui::LogToFile();

					ImGui::SliderFloat("Rotation XOY", &ui_states::rotation_theta, 0.0f, DirectX::g_XMTwoPi.f[0]);
					ImGui::SliderFloat("Rotation Z", &ui_states::rotation_phi, 0.0f, DirectX::g_XMTwoPi.f[0]);

					ImGui::ColorEdit3("Remap", ui_states::remap);
					mainproc::renderer.set_remap({ static_cast<byte>(ui_states::remap[0] * 255),static_cast<byte>(ui_states::remap[1] * 255),static_cast<byte>(ui_states::remap[2] * 255) });

					ImGui::SliderFloat("Turret Rotation", &ui_states::turret_rotation, 0.0f, DirectX::g_XMTwoPi.f[0]);

					if (ImGui::Button("Reload"))
					{
						assets::vxl.load(assets::vxl_path.string());
						assets::hva.load(assets::hva_path.string());
						assets::tur_vxl.load(assets::tur_path.string());
						assets::tur_hva.load(assets::tur_hvapath.string());
						assets::barl_vxl.load(assets::barl_path.string());
						assets::barl_hva.load(assets::barl_hvapath.string());

						mainproc::renderer.load_vxl(assets::vxl, assets::hva, 0, true);
						mainproc::renderer.load_vxl(assets::tur_vxl, assets::tur_hva, 0);
						mainproc::renderer.load_vxl(assets::barl_vxl, assets::barl_hva, 0);
					}

					ImGui::SameLine();
					if (!ui_states::playing && ImGui::Button("Play"))
					{
						ui_states::playing = true;
					}
					else if (ui_states::playing && ImGui::Button("Stop"))
					{
						ui_states::playing = false;
					}

					ImGui::SameLine();
					if (ImGui::Button("Screenshot(buxved)"))
					{

					}

					ImGui::SameLine();
					if (ImGui::Button("Reset"))
					{
						ui_states::reset_checked = true;
					}

					static const auto rotation = DirectX::XMMatrixRotationZ(-0.25f * vgm::pi()) * DirectX::XMMatrixRotationX(vgm::pi() / 6.0f);
					static const auto rotationi = DirectX::XMMatrixTranspose(rotation);

					ImGui::Checkbox("Dir panel locked", &ui_states::direction_panel_locked);
					if (ui_states::direction_panel_locked)
						ImGui::BeginDisabled();

					vgm::Vec3 direction = ui_states::light_direction;
					DirectX::XMVECTOR dir = { direction.x,direction.y,direction.z,0.0f };
					dir = DirectX::XMVector3TransformNormal(dir, rotation);

					direction = { dir.vector4_f32[0],dir.vector4_f32[1],dir.vector4_f32[2] };
					direction = { -direction.x,-direction.z,direction.y };//to GUI space
					direction = -direction;
					ImGui::gizmo3D("Light Dir", direction);

					direction = -direction;
					direction = { -direction.x,direction.z,-direction.y };//to Game space
					dir = { direction.x,direction.y,direction.z,0.0f };
					dir = DirectX::XMVector3TransformNormal(dir, rotationi);
					direction = { dir.vector4_f32[0],dir.vector4_f32[1],dir.vector4_f32[2] };

					if (!ui_states::direction_panel_locked)
						ui_states::light_direction = direction;
					else
						ImGui::EndDisabled();

					ImGui::InputFloat3("Light Direction", (float*)ui_states::light_direction);

					ImGui::InputFloat("Extra light", &ui_states::extra_light);
					ImGui::InputFloat("Turret Offset", &ui_states::turret_offset);
					ImGui::InputFloat("Model Scale", &ui_states::scale);
					
					ImGui::NewLine();

					std::vector<const char*> names;
					for (const auto& set : ui_states::color_sets)
						names.push_back(set.name.c_str());

					ImGui::Combo("Color Set", reinterpret_cast<int*>(&ui_states::color_set_idx), names.data(), names.size());

					auto& colorset = ui_states::color_sets[ui_states::color_set_idx];
					ImGui::SliderFloat("Ambient", &colorset.ambient, 0.0f, 5.0f);
					ImGui::SliderFloat("Diffuse", &colorset.diffuse, 0.0f, 5.0f);
					ImGui::SliderFloat("Specular", &colorset.specular, 0.0f, 5.0f);

					if (ImGui::Button("Update VPL"))
					{
						update_vpl();
						mainproc::renderer.load_vpl(assets::vpl);
					}

					ImGui::SameLine();
					if (ImGui::Button("Save VPL"))
					{
						assets::vpl.save(select_folder());
					}

					ImGui::SameLine();
					ImGui::Checkbox("Show Color Selection Panel", &ui_states::show_colorsel_panel);

					if(ui_states::show_colorsel_panel)
						PaletteSelectionPanel("Select Color", assets::pal, colorset.color_selection);

					//ImGui::LogFinish();
					ImGui::End();
				}

				ImGui::Render();

				if (ui_states::reset_checked)
				{
					ui_states::reset_values();
					ui_states::reset_checked = false;
				}

				mainproc::renderer.set_extra_light(ui_states::extra_light);

				const auto& direction = ui_states::light_direction;
				const float scale = ui_states::scale;
				mainproc::renderer.set_scale_factor({ scale,scale,scale,scale });
				auto rotation_y = DirectX::XMMatrixRotationY(-ui_states::rotation_phi),
					rotation_xy = DirectX::XMMatrixRotationAxis({ 1.0f,1.0f,0.0f,0.0f }, -ui_states::xy_angle),
					rotation_z = DirectX::XMMatrixRotationZ(-ui_states::z_angle - ui_states::rotation_theta);
				mainproc::renderer.set_world(rotation_z * rotation_y * rotation_xy);
				mainproc::renderer.set_light_dir({ direction.x,direction.y,direction.z,0.0f });


				size_t& current_frame = ui_states::current_frame;
				const float reload_Z = -ui_states::turret_rotation;
				const float rotations[] = { 0.0f,reload_Z,reload_Z };
				const hva* hvas[] = { &assets::hva,&assets::tur_hva,&assets::barl_hva };
				const size_t frames[] = { current_frame,current_frame,current_frame };
				const float offsets[] = { 0.0f, ui_states::turret_offset * pixels_to_leptons,ui_states::turret_offset * pixels_to_leptons };

				static_assert(_countof(hvas) == _countof(frames), "HVA array mismatch.");

				auto time = std::chrono::high_resolution_clock::now();
				if (ui_states::playing && time - last_tick > std::chrono::milliseconds(67u))
					++current_frame, last_tick = time;

				mainproc::renderer.reload_hva(hvas, frames, rotations, offsets, _countof(hvas));

				mainproc::renderer.clear_vxl_canvas();
				mainproc::renderer.render_loaded_vxl();
				mainproc::renderer.render_gui();
				mainproc::renderer.present();
			}

			ImGui_ImplWin32_Shutdown();
			mainproc::renderer.clear_renderer();
			ImGui::DestroyContext();
		}

		DestroyWindow(mainwin);
		mainproc::mainwin = NULL;
	}
	
	UnregisterClass(classname, NULL);
	CoUninitialize();

	save_vpl_setting_cache();
	return 0;
}