#include "d3d.h"
#include "gdi.h"
#include "hva.h"
#include "vxl.h"
#include "vpl.h"
#include "config.h"

#include "stb_includer.h"

#include <CommCtrl.h>
#include <windowsx.h>
#include <chrono>

#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "stb.lib")

struct color_key
{
	std::string name = {};
	color color = {};
};

namespace test_window
{
	vxl_gdi_renderer renderer;
	vpl_renderer renderer2;
	HWND mainwin = NULL;
	HWND canvas = NULL;
	HWND trackbar_theta = NULL;
	HWND trackbar_phi = NULL;
	HWND trackbar_turrot = NULL;
	HWND color_sel = NULL;
	HWND reset_button = NULL;
	HWND play_button = NULL;
	HWND shot_button = NULL;
	HWND light_box1 = NULL, light_box2 = NULL, light_box3 = NULL;
	bool stop = true;
	bool repaint_required = true;
	std::unordered_map<std::string, color> colors = {};
	size_t current_color = 0;
	size_t theta_value = 0;
	size_t phi_value = 50;
	size_t turret_rot_value = 0;
	size_t current_hva_frame = 0;
}

namespace shot
{
	size_t directions = 8;
	std::string output_dir = "output";
	std::string filename;
	bool generate_ingame_like_previews = false;
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
}

void init_console()
{
	AllocConsole();
	UNREFERENCED_PARAMETER(freopen("CONOUT$", "w", stdout));
	UNREFERENCED_PARAMETER(freopen("CONIN$", "r", stdin));
}

void disable_console()
{
	fclose(stdout);
	fclose(stdin);
	FreeConsole();
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
	auto& renderer = test_window::renderer2;

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

	const float reload_Z = static_cast<float>(test_window::turret_rot_value) * DirectX::g_XMTwoPi.f[0] / 100.0f;
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

			renderer.reload_hva(hvas, frames, rotations, _countof(hvas));
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

		renderer.reload_hva(hvas, frames, rotations, _countof(hvas));
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

LRESULT __stdcall wnd_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static float zangle = 0.0f;
	static float xyangle = 0.0f;
	static float scale = 1.0f;
	static bool changed = false;
	static bool reset_required = false;

	if (msg == WM_PAINT)
	{
		PAINTSTRUCT paint = {};
		BeginPaint(window, &paint); 

		if (window == test_window::canvas)
		{
			size_t new_theta = reset_required ? 0 : SendMessageW(test_window::trackbar_theta, TBM_GETPOS, 0, 0);
			size_t new_phi = reset_required ? 50 : SendMessageW(test_window::trackbar_phi, TBM_GETPOS, 0, 0);
			size_t new_rot = reset_required ? 0 : SendMessageW(test_window::trackbar_turrot, TBM_GETPOS, 0, 0);
			size_t new_col = SendMessageW(test_window::color_sel, CB_GETCURSEL, 0, 0);

			xyangle = reset_required ? 0.0f : xyangle;
			zangle = reset_required ? 0.0f : zangle;
			scale = reset_required ? 1.0f : scale;
			if (reset_required)
			{
				SendMessageW(test_window::trackbar_phi, TBM_SETPOS, TRUE, new_phi);
				SendMessageW(test_window::trackbar_theta, TBM_SETPOS, TRUE, new_theta);
				SendMessageW(test_window::trackbar_turrot, TBM_SETPOS, TRUE, new_rot);
			}

			if (test_window::repaint_required || reset_required || new_rot != test_window::turret_rot_value || 
				new_theta != test_window::theta_value || new_phi != test_window::phi_value || changed || new_col != test_window::current_color)
			{
				test_window::theta_value = new_theta;
				test_window::phi_value = new_phi;
				test_window::current_color = new_col;
				test_window::turret_rot_value = new_rot;
				test_window::repaint_required = false;
				changed = false;
				reset_required = false;

				DirectX::XMMATRIX rotate_y = {}, rotate_z = {}, rotate_xy = {};
				rotate_y = DirectX::XMMatrixRotationY((static_cast<float>(new_phi) - 50.0f) * DirectX::g_XMPi.f[0] / 50.0f);
				//rotate_z = DirectX::XMMatrixRotationZ(static_cast<float>(new_theta) * DirectX::g_XMTwoPi.f[0] / 100.0f);
				rotate_xy = DirectX::XMMatrixRotationAxis({ 1.0f,-1.0f,0.0f,0.0f }, xyangle);
				rotate_z = DirectX::XMMatrixRotationZ(zangle + static_cast<float>(new_theta) * DirectX::g_XMTwoPi.f[0] / 100.0f);
				//auto dir = DirectX::XMVECTOR{ 0.0f,1.0f,0.0f,0.0f };
				//dir = DirectX::XMVector4Transform(dir, rotate_y * rotate_z);
				size_t color_string_len = SendMessageA(test_window::color_sel, CB_GETLBTEXTLEN, new_col, 0);
				std::string color_name;
				color_name.resize(color_string_len);
				SendMessageA(test_window::color_sel, CB_GETLBTEXT, new_col, reinterpret_cast<LPARAM>(color_name.data()));

				const float& current_frame = test_window::current_hva_frame;
				const float reload_Z = static_cast<float>(new_rot) * DirectX::g_XMTwoPi.f[0] / 100.0f;
				const float rotations[] = { 0.0f,reload_Z,reload_Z };
				const hva* hvas[] = { &assets::hva,&assets::tur_hva,&assets::barl_hva };
				const size_t frames[] = { current_frame,current_frame,current_frame };
				test_window::renderer2.reload_hva(hvas, frames, rotations, _countof(rotations));
				
				auto find = test_window::colors.find(color_name);
				if (find != test_window::colors.end())
					test_window::renderer2.set_remap(find->second);

				test_window::renderer2.set_world(/*rotate_y*/ rotate_z* rotate_y * rotate_xy);
				test_window::renderer2.set_scale_factor({ scale,scale,scale,scale });
				//test_window::renderer.set_world(rotate_y * rotate_z);
				//test_window::renderer.render_vxl(assets::vxl, assets::hva, assets::pal, assets::vpl, 0);

				test_window::renderer2.clear_vxl_canvas();
				test_window::renderer2.render_loaded_vxl();
				test_window::renderer2.present();
			}

			InvalidateRect(window, nullptr, false);
			//test_window::renderer.copy_result(GetDC(window));
		}

		EndPaint(window, &paint);
		return 0;
	}
	else if (msg == WM_MOUSEMOVE)
	{
		static bool track_started = false;
		static POINT last_position = { 0,0 };

		if ((wparam & MK_LBUTTON) && window == test_window::canvas)
		{
			if (!track_started)
			{
				track_started = true;
				last_position = { LOWORD(lparam),HIWORD(lparam) };
				SetCapture(window);
			}
			else
			{
				zangle -= (GET_X_LPARAM(lparam) - last_position.x) * DirectX::g_XMPi.f[0] / 80.0f;
				xyangle -= (GET_Y_LPARAM(lparam) - last_position.y) * DirectX::g_XMTwoPi.f[0] / 160.0f;

				if (std::abs(zangle) > DirectX::g_XMTwoPi.f[0])
					zangle += -zangle / std::abs(zangle) * DirectX::g_XMTwoPi.f[0];
				if (std::abs(xyangle) > DirectX::g_XMTwoPi.f[0])
					xyangle += -xyangle / std::abs(xyangle) * DirectX::g_XMTwoPi.f[0];

				last_position = { GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam) };
				changed = true;
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
	else if (msg == WM_MOUSEWHEEL)
	{
		if (window == test_window::canvas)
		{
			float delta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 0.25f;
			scale += delta;
			scale = std::max(scale, 0.2f);
			changed = true;
			return 0;
		}
	}
	else if (msg == WM_COMMAND)
	{
		if (window == test_window::mainwin && HIWORD(wparam) == BN_CLICKED)
		{
			if ((HWND)lparam == test_window::reset_button)
				reset_required = true;
			else if ((HWND)lparam == test_window::play_button)
			{
				if (test_window::stop)
					Button_SetText(test_window::play_button, TEXT("Stop"));
				else
					Button_SetText(test_window::play_button, TEXT("Play"));

				test_window::stop = !test_window::stop;
			}
			else if ((HWND)lparam == test_window::shot_button)
			{
				screen_shot(shot::filename, shot::output_dir);
			}
		}
	}
	else if (msg == WM_CLOSE)
	{
		test_window::renderer2.clear_renderer();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(window, msg, wparam, lparam);
}

void generate_normal_table_codes(const std::string& exe_name)
{
	std::ofstream output(".\\normal_table.hpp");
	std::ifstream executable(exe_name, std::ios::binary);

	if (!output.is_open() || !executable.is_open())
		return;

	static const uint32_t code_offset = 0x446f78u;

	std::unique_ptr<char> read_buffer(new char[245 * 3 * sizeof(float)]);
	if (!read_buffer)
		return;

	output << "DirectX::XMVECTOR game_normals[256] = {\n";
	executable.seekg(code_offset);
	executable.read(read_buffer.get(), 245 * 3 * sizeof(float));

	//	{ 0.5266f,	-0.3596f,	-0.7703f,	0.0000f	},
	const float* read = reinterpret_cast<float*>(read_buffer.get());
	size_t float_index = 0;
	for (size_t i = 0; i < 256; i++)
	{
		if (i < 245)
			output << "\t{\t" << read[float_index++] << "f,\t" << read[float_index++] << "f,\t" << read[float_index++] << "f,\t0.0000f\t},\n";
		else
			output << "\t{\t0.0000f,\t0.0000f,\t1.0000f,\t0.0000f\t},\n";
	}
	output << "};";
	output.close();
	executable.close();
}

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmdline, int cmdshow)
{
#if _DEBUG
	init_console();
#endif
	logger::initialize();

	std::filesystem::path current_dir = get_exe_path();
	assets::vpl.load((current_dir / "voxels.vpl").string());
	assets::pal.load((current_dir / "unittem.pal").string());
	assets::ini.load((current_dir / "settings.ini").string());

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
	filepath.replace_extension("hva");
	assets::hva.load(filepath.string());

	filepath.replace_filename(base_filename + "tur");
	filepath.replace_extension("vxl");
	assets::tur_vxl.load(filepath.string());
	filepath.replace_extension("hva");
	assets::tur_hva.load(filepath.string());

	filepath.replace_filename(base_filename + "barl");
	filepath.replace_extension("vxl");
	assets::barl_vxl.load(filepath.string());
	filepath.replace_extension("hva");
	assets::barl_hva.load(filepath.string());

	WNDCLASSEX wndclass = {};
	wndclass.cbSize = sizeof wndclass;
	wndclass.lpszClassName = TEXT("No name");
	wndclass.lpfnWndProc = wnd_proc;
	wndclass.hInstance = instance;
	//wndclass.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
	wndclass.style = CS_PARENTDC;
	RegisterClassEx(&wndclass);

	InitCommonControls();
	HWND window = CreateWindow(TEXT("No name"), TEXT("__ ____ _ ____ __ _"),
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), 
		300, 300, vxl_gdi_renderer::bitmap_width + 300, vxl_gdi_renderer::bitmap_height + 100, NULL, NULL, NULL, NULL);

	if (window)
	{
		test_window::mainwin = window;
		HWND canvas = CreateWindow(TEXT("No name"), TEXT("CANVAS"), WS_CHILD, 25, 25,
			vxl_gdi_renderer::bitmap_width, vxl_gdi_renderer::bitmap_height, window, NULL, NULL, NULL);

		HWND trackbar_phi = CreateWindow(TRACKBAR_CLASS, TEXT("TRACKBAR_PHI"), WS_CHILD | WS_VISIBLE, 306, 80, 150, 30, window, NULL, NULL, NULL);
		HWND trackbar_theta = CreateWindow(TRACKBAR_CLASS, TEXT("TRACKBAR_THETA"), WS_CHILD | WS_VISIBLE, 306, 30, 150, 30, window, NULL, NULL, NULL);
		HWND trackbar_rot = CreateWindow(TRACKBAR_CLASS, TEXT("TRACKBAR_ROTATION"), WS_CHILD | WS_VISIBLE, 306, 220, 150, 30, window, NULL, NULL, NULL);
		HWND color_sel = CreateWindow(WC_COMBOBOX, TEXT("COLOR_SEL"), WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 306, 130, 100, 120, window, NULL, NULL, NULL);
		HWND reset_btn = CreateWindow(WC_BUTTON, TEXT("RESET"), WS_CHILD | BS_CENTER, 426, 130, 100, 25, window, NULL, NULL, NULL);
		HWND play_btn = CreateWindow(WC_BUTTON, TEXT("PLAY"), WS_CHILD | BS_CENTER, 306, 180, 100, 25, window, NULL, NULL, NULL);
		HWND shot_button = CreateWindow(WC_BUTTON, TEXT("SHOT"), WS_CHILD | BS_CENTER, 426, 180, 100, 25, window, NULL, NULL, NULL);

		DWORD edit_style = WS_DLGFRAME | WS_CHILD | ES_LEFT;
		HWND light_box1 = CreateWindow(WC_EDIT, TEXT("BOX1"), edit_style, 305, 220, 70, 30, window, NULL, NULL, NULL);
		HWND light_box2 = CreateWindow(WC_EDIT, TEXT("BOX2"), edit_style, 380, 220, 70, 30, window, NULL, NULL, NULL);
		HWND light_box3 = CreateWindow(WC_EDIT, TEXT("BOX3"), edit_style, 455, 220, 70, 30, window, NULL, NULL, NULL);

		ShowWindow(window, SW_SHOW);
		ShowWindow(canvas, SW_SHOW);
		ShowWindow(trackbar_phi, SW_SHOW);
		ShowWindow(trackbar_theta, SW_SHOW);
		ShowWindow(color_sel, SW_SHOW);
		ShowWindow(reset_btn, SW_SHOW);
		ShowWindow(play_btn, SW_SHOW);
		ShowWindow(shot_button, SW_SHOW);

		SendMessageW(trackbar_phi, TBM_SETRANGEMIN, FALSE, 0);
		SendMessageW(trackbar_phi, TBM_SETRANGEMAX, FALSE, 100);
		SendMessageW(trackbar_phi, TBM_SETPOS, TRUE, test_window::phi_value);

		SendMessageW(trackbar_theta, TBM_SETRANGEMIN, FALSE, 0);
		SendMessageW(trackbar_theta, TBM_SETRANGEMAX, FALSE, 100);
		SendMessageW(trackbar_theta, TBM_SETPOS, TRUE, test_window::theta_value + 1);
		SendMessageW(trackbar_theta, TBM_SETPOS, TRUE, test_window::theta_value);

		SendMessageW(trackbar_rot, TBM_SETRANGEMIN, FALSE, 0);
		SendMessageW(trackbar_rot, TBM_SETRANGEMAX, FALSE, 100);
		SendMessageW(trackbar_rot, TBM_SETPOS, TRUE, test_window::turret_rot_value + 1);
		SendMessageW(trackbar_rot, TBM_SETPOS, TRUE, test_window::turret_rot_value);

		Button_SetText(reset_btn, TEXT("Reset"));
		Button_SetText(play_btn, TEXT("Play"));
		Button_SetText(shot_button, TEXT("Screenshot"));

		color temp = {};
		temp.r = 252u;
		temp.g = 0u;
		temp.b = 0u;
		
		test_window::colors["Default"] = temp;
		SendMessageA(color_sel, CB_ADDSTRING, NULL, reinterpret_cast<LPARAM>("Default"));
		SendMessageA(color_sel, CB_SETCURSEL, 0, NULL);//CB_SETMINVISIBLE 
		SendMessageA(color_sel, CB_SETMINVISIBLE, 1, NULL); 
		if (assets::ini.is_loaded())
		{
			const auto sec_name = "Colors";
			const auto section = assets::ini.section(sec_name);
			for (const auto& pairs : section)
			{
				const auto values = assets::ini.value_as_int(sec_name, pairs.first);
				if (values.size() >= 3)
				{
					temp.r = static_cast<byte>(std::clamp(values[0], 0, 255));
					temp.g = static_cast<byte>(std::clamp(values[1], 0, 255));
					temp.b = static_cast<byte>(std::clamp(values[2], 0, 255));

					bool new_string = false;
					auto find = test_window::colors.find(pairs.first);
					if (find == test_window::colors.end())
						new_string = true;

					test_window::colors[pairs.first] = temp;
					if(new_string)
						SendMessageA(color_sel, CB_ADDSTRING, NULL, reinterpret_cast<LPARAM>(pairs.first.c_str()));
				}
			}

			const auto setting = "Settings";
			shot::output_dir = assets::ini.read_string(setting, "ScreenshotOutputDir", shot::output_dir);
			shot::directions = assets::ini.read_int(setting, "DirectionCount", shot::directions);
			shot::bgfilename = assets::ini.read_string(setting, "BackgroundFileName", shot::bgfilename);
			shot::celloffsetx = static_cast<size_t>(std::abs(assets::ini.read_int(setting, "CellOffsetX", shot::celloffsetx)));
			shot::celloffsety = static_cast<size_t>(std::abs(assets::ini.read_int(setting, "CellOffsetY", shot::celloffsety)));

			DirectX::XMVECTOR setting_color = { 0.0f,0.0f,1.0f,1.0f };
			auto bgcolor = assets::ini.value_as_int(setting, "BackgroundColor");
			if (bgcolor.size() >= 4)
			{
				for (size_t i = 0; i < 4; i++)
					setting_color.vector4_f32[i] = static_cast<float>(std::clamp(bgcolor[i], 0, 255)) / 255.0f;
				test_window::renderer2.set_bg_color(setting_color);
			}

			shot::generate_ingame_like_previews = assets::ini.read_bool(setting, "GenerateIngameViews", shot::generate_ingame_like_previews);
		}

		UpdateWindow(color_sel);
		UpdateWindow(reset_btn);
		UpdateWindow(play_btn);
		UpdateWindow(shot_button);
		test_window::canvas = canvas;
		test_window::trackbar_theta = trackbar_theta;
		test_window::trackbar_phi = trackbar_phi;
		test_window::trackbar_turrot = trackbar_rot;
		test_window::color_sel = color_sel;
		test_window::reset_button = reset_btn;
		test_window::play_button = play_btn;
		test_window::shot_button = shot_button;
		test_window::light_box1 = light_box1;
		test_window::light_box2 = light_box2;
		test_window::light_box3 = light_box3;

		if (!test_window::renderer2.initialize(canvas))
			LOG(ERROR) << "D3D initialization failed.\n";

		if(!test_window::renderer2.load_pal(assets::pal))
			LOG(ERROR) << "Palette not loaded.\n";

		if (!test_window::renderer2.load_vpl(assets::vpl))
			LOG(ERROR) << "VPL not loaded.\n";

		if (!test_window::renderer2.load_vxl(assets::vxl, assets::hva, 0, true))
			LOG(ERROR) << "VXL&HVA not loaded.\n";

		test_window::renderer2.load_vxl(assets::tur_vxl, assets::tur_hva, 0);
		test_window::renderer2.load_vxl(assets::barl_vxl, assets::barl_hva, 0);

		if (!test_window::renderer2.render_loaded_vxl())
			LOG(ERROR) << "First render.\n";

		if (!test_window::renderer.initialize(canvas))
			LOG(ERROR) << "GDI initialization.\n";

		/*if (!test_window::renderer.render_vxl(assets::vxl, assets::hva, assets::pal, assets::vpl, 0))
			LOG(ERROR) << "GDI render.\n";*/

		auto default_light = test_window::renderer2.get_light_dir();
		Edit_SetText(light_box1, std::to_wstring(default_light.vector4_f32[0]).c_str());
		Edit_SetText(light_box2, std::to_wstring(default_light.vector4_f32[1]).c_str());
		Edit_SetText(light_box3, std::to_wstring(default_light.vector4_f32[2]).c_str());
		Edit_Enable(light_box1, FALSE);
		Edit_Enable(light_box2, FALSE);
		Edit_Enable(light_box3, FALSE);

		UpdateWindow(light_box1);
		UpdateWindow(light_box2);
		UpdateWindow(light_box3);

		MSG msg = {};
		size_t& current_frame = test_window::current_hva_frame;
		auto last_tick = std::chrono::high_resolution_clock::now();
		while (true)
		{
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
					break;

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			auto time = std::chrono::high_resolution_clock::now();
			auto duration = time - last_tick;
			if (!test_window::stop && duration > std::chrono::milliseconds(67))
			{
				last_tick = time;
				const float reload_Z = static_cast<float>(test_window::turret_rot_value) * DirectX::g_XMTwoPi.f[0] / 100.0f;
				const float rotations[] = { 0.0f,reload_Z,reload_Z };
				const hva* hvas[] = { &assets::hva,&assets::tur_hva,&assets::barl_hva };
				const size_t frames[] = { current_frame,current_frame,current_frame };
				
				static_assert(_countof(hvas) == _countof(frames), "HVA array mismatch.");

				++current_frame;
				test_window::renderer2.reload_hva(hvas, frames, rotations, _countof(hvas));
				test_window::repaint_required = true;
			}
		}

		DestroyWindow(window);
		DestroyWindow(canvas);
		DestroyWindow(trackbar_theta);
		DestroyWindow(trackbar_phi);
		DestroyWindow(trackbar_rot);
		DestroyWindow(color_sel);
		DestroyWindow(reset_btn);
		DestroyWindow(play_btn);
		DestroyWindow(shot_button);
		DestroyWindow(light_box1);
		DestroyWindow(light_box2);
		DestroyWindow(light_box3);

		test_window::renderer2.clear_renderer();
		test_window::canvas = test_window::mainwin = test_window::trackbar_phi = 
			test_window::trackbar_theta = test_window::color_sel = test_window::reset_button = 
			test_window::play_button =  test_window::trackbar_turrot = test_window::shot_button = 
			test_window::light_box1 = test_window::light_box2 = test_window::light_box3 = NULL;
	}
	
#if _DEBUG
	disable_console();
#endif
	UnregisterClass(TEXT("No name"), instance);
	logger::uninitialize();
	return 0;
}