#include "debugWindow.h"
#include "gizmo.h"
#include "graphics/renderer.h"
#include "sceneGraph.h"
#include "selection.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdexcept>

LRESULT CALLBACK DebugWindow::main_proc(HWND hwnd, UINT message, WPARAM wp,
										LPARAM lp)
{
	auto* editor =
		static_cast<DebugWindow*>(GetPropW(hwnd, L"VulkanWork.EditorMenu"));
	if (!editor) return DefWindowProcW(hwnd, message, wp, lp);
	switch (message)
	{
		case WM_COMMAND:
			if (!lp && LOWORD(wp) >= MenuNew && LOWORD(wp) <= MenuReset)
			{
				editor->queue_menu_command(LOWORD(wp));
				return 0;
			}
			break;
		case WM_INITMENUPOPUP:
			editor->update_menu();
			break;
		case WM_ENTERMENULOOP:
			editor->menuActive_ = true;
			break;
		case WM_EXITMENULOOP:
			editor->menuActive_ = false;
			editor->menuEscape_ = (GetKeyState(VK_ESCAPE) & 0x8000) != 0;
			break;
		case WM_KEYDOWN:
			if (editor->menu_key(wp, lp)) return 0;
			break;
	}
	return CallWindowProcW(editor->mainProc_, hwnd, message, wp, lp);
}
void DebugWindow::init_menu()
{
	menuBar_ = CreateMenu();
	if (!menuBar_)
		throw std::runtime_error("Could not create the application menu");
	auto item = [](HMENU menu, UINT command, const wchar_t* title)
	{ AppendMenuW(menu, MF_STRING, command, title); };
	auto separator = [](HMENU menu)
	{ AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); };
	auto submenu = [](HMENU menu, const wchar_t* title)
	{
		auto child = CreatePopupMenu();
		AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child), title);
		return child;
	};
	auto file = submenu(menuBar_, L"&File");
	item(file, MenuNew, L"&New Scene\tCtrl+N");
	item(file, MenuOpen, L"&Open Scene...\tCtrl+O");
	separator(file);
	item(file, MenuSave, L"&Save Scene\tCtrl+S");
	item(file, MenuSaveAs, L"Save Scene &As...\tCtrl+Shift+S");
	separator(file);
	auto import = submenu(file, L"&Import");
	item(import, MenuImportMesh, L"&Mesh...\tCtrl+I");
	item(import, MenuImportSplat, L"Gaussian &Splat...\tCtrl+Shift+I");
	separator(file);
	item(file, MenuExit, L"E&xit\tAlt+F4");
	auto edit = submenu(menuBar_, L"&Edit");
	item(edit, MenuDelete, L"&Delete Selection\tDelete");
	item(edit, MenuDeselect, L"D&eselect\tCtrl+Shift+A");
	separator(edit);
	auto transform = submenu(edit, L"&Transform");
	item(transform, MenuTranslate, L"&Translate\tW");
	item(transform, MenuRotate, L"&Rotate\tE");
	item(transform, MenuScale, L"&Scale\tR");
	separator(transform);
	item(transform, MenuWorld, L"&World Space");
	item(transform, MenuLocal, L"&Local Space");
	separator(transform);
	item(transform, MenuSnap, L"S&napping");
	auto add = submenu(menuBar_, L"&Add");
	auto light = submenu(add, L"&Light");
	item(light, MenuDirectional, L"&Directional");
	item(light, MenuPoint, L"&Point");
	item(light, MenuSpot, L"&Spot");
	auto view = submenu(menuBar_, L"&View");
	item(view, MenuHeatmap, L"Tile &Heatmap");
	item(view, MenuWireframes, L"Light &Wireframes");
	item(view, MenuBounds, L"Selection &Bounds");
	separator(view);
	item(view, MenuSkipDepth, L"Skip &Depth Prepass");
	item(view, MenuCulling, L"Disable Backface &Culling");
	auto window = submenu(menuBar_, L"&Window");
	item(window, MenuScenePane, L"&Scene / Object");
	item(window, MenuMaterialPane, L"&Material");
	item(window, MenuOptionsPane, L"&Options");
	separator(window);
	auto floating = submenu(window, L"&Float Pane");
	item(floating, MenuFloatScene, L"&Scene / Object");
	item(floating, MenuFloatMaterial, L"&Material");
	item(floating, MenuFloatOptions, L"&Options");
	separator(window);
	item(window, MenuDockAll, L"&Dock All Panes");
	item(window, MenuReset, L"&Reset Pane Layout");
	HWND parent = glfwGetWin32Window(parent_);
	RECT client{};
	GetClientRect(parent, &client);
	SetPropW(parent, L"VulkanWork.EditorMenu", this);
	mainProc_ = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrW(parent, GWLP_WNDPROC,
						  reinterpret_cast<LONG_PTR>(&DebugWindow::main_proc)));
	SetMenu(parent, menuBar_);
	// Keep the saved client size stable across launches when adding chrome.
	if (!IsZoomed(parent))
	{
		AdjustWindowRectEx(&client, DWORD(GetWindowLongPtrW(parent, GWL_STYLE)),
						   TRUE, DWORD(GetWindowLongPtrW(parent, GWL_EXSTYLE)));
		SetWindowPos(
			parent, nullptr, 0, 0, client.right - client.left,
			client.bottom - client.top,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
	DrawMenuBar(parent);
}
void DebugWindow::queue_menu_command(UINT command)
{
	actions_.push_back([this, command] { execute_menu_command(command); });
}
bool DebugWindow::menu_key(WPARAM key, LPARAM flags)
{
	if (menuActive_ ||
		glfwGetInputMode(parent_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
		return false;
	bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	if (GetKeyState(VK_MENU) & 0x8000) return false;
	UINT command = 0;
	if (ctrl)
	{
		if (key == 'N' && !shift)
			command = MenuNew;
		else if (key == 'O' && !shift)
			command = MenuOpen;
		else if (key == 'S')
			command = shift ? MenuSaveAs : MenuSave;
		else if (key == 'I')
			command = shift ? MenuImportSplat : MenuImportMesh;
		else if (key == 'A' && shift)
			command = MenuDeselect;
	}
	else
	{
		// Editing fields retain Delete and character keys. In the viewport
		// and non-editing pane areas these operate on the current selection.
		for (const auto& pane : panes_)
			if (GetFocus() == BK_window_handle(pane.window) &&
				BK_focus(pane.context))
				return false;
		if (key == VK_DELETE)
			command = MenuDelete;
		else if (key == 'W')
			command = MenuTranslate;
		else if (key == 'E')
			command = MenuRotate;
		else if (key == 'R')
			command = MenuScale;
	}
	if (!command) return false;
	if (!(flags & (LPARAM(1) << 30))) queue_menu_command(command);
	return true;
}
void DebugWindow::update_menu()
{
	bool selected = menuSelection_ && menuScene_ &&
					menuSelection_->selectedNode &&
					*menuSelection_->selectedNode < menuScene_->nodes.size();
	for (UINT command : {UINT(MenuDelete), UINT(MenuDeselect)})
		EnableMenuItem(menuBar_, command,
					   MF_BYCOMMAND | (selected ? MF_ENABLED : MF_GRAYED));
	auto check = [&](UINT command, bool value)
	{
		CheckMenuItem(menuBar_, command,
					  MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
	};
	if (menuGizmo_)
	{
		CheckMenuRadioItem(menuBar_, MenuTranslate, MenuScale,
						   MenuTranslate + UINT(menuGizmo_->operation),
						   MF_BYCOMMAND);
		CheckMenuRadioItem(
			menuBar_, MenuWorld, MenuLocal,
			menuGizmo_->space == Gizmo::Space::World ? MenuWorld : MenuLocal,
			MF_BYCOMMAND);
		check(MenuSnap, menuGizmo_->useSnap);
	}
	if (menuRenderer_)
	{
		check(MenuHeatmap, menuRenderer_->showHeatmap_);
		check(MenuWireframes, menuRenderer_->showDebugLines_);
		check(MenuBounds, menuRenderer_->showSelectionBounds_);
		check(MenuSkipDepth, menuRenderer_->debugSkipDepthPrepass_);
		check(MenuCulling, menuRenderer_->debugDisableCulling_);
	}
	for (int i = 0; i < PaneCount; ++i)
		check(MenuFloatScene + i, panes_[i].floating);
}
void DebugWindow::reset_layout()
{
	dockWidth_ = -1;
	columnRatio_ = .5f;
	for (auto& pane : panes_) pane.sizeWeight = 1;
	dock_pane(panes_[Scene], 0, 0);
	dock_pane(panes_[Material], 1, 0);
	dock_pane(panes_[Options], 1, 1);
}
void DebugWindow::execute_menu_command(UINT command)
{
	// Commit any pending field edit before scene/file actions read its value.
	if (command >= MenuNew && command <= MenuExit)
		for (auto& pane : panes_) BK_focus_node(pane.context, nullptr);
	switch (command)
	{
		case MenuNew:
			if (newScene) newScene();
			break;
		case MenuOpen:
			if (loadScene) loadScene();
			break;
		case MenuSave:
			if (saveScene) saveScene();
			break;
		case MenuSaveAs:
			if (saveAs) saveAs();
			break;
		case MenuImportMesh:
			importRequested = true;
			break;
		case MenuImportSplat:
			importSplatRequested = true;
			break;
		case MenuExit:
			if (exit) exit();
			break;
		case MenuDelete:
			if (menuSelection_ && menuSelection_->selectedNode)
				deleteRequested = true;
			break;
		case MenuDeselect:
			if (menuSelection_) menuSelection_->selectedNode.reset();
			break;
		case MenuTranslate:
		case MenuRotate:
		case MenuScale:
			if (menuGizmo_)
				menuGizmo_->operation = Gizmo::Op(command - MenuTranslate);
			break;
		case MenuWorld:
		case MenuLocal:
			if (menuGizmo_)
				menuGizmo_->space = command == MenuWorld ? Gizmo::Space::World
														 : Gizmo::Space::Local;
			break;
		case MenuSnap:
			if (menuGizmo_) menuGizmo_->useSnap = !menuGizmo_->useSnap;
			break;
		case MenuDirectional:
		case MenuPoint:
		case MenuSpot:
			if (menuScene_ && menuSelection_)
			{
				LightComponent light;
				light.type = LightType(command - MenuDirectional);
				light.intensity =
					light.type == LightType::Directional ? 3.f : 5.f;
				// Drop old bindings before inserting into the scene node
				// vector.
				invalidate();
				menuSelection_->selectedNode = menuScene_->add_light_node(
					std::string(light_type_name(light.type)) + " Light", light,
					make_light_transform({0, 3, 0}, {0, -1, 0}));
				menuScene_->update_world_transforms();
			}
			break;
		case MenuHeatmap:
			if (menuRenderer_)
				menuRenderer_->showHeatmap_ = !menuRenderer_->showHeatmap_;
			break;
		case MenuWireframes:
			if (menuRenderer_)
				menuRenderer_->showDebugLines_ =
					!menuRenderer_->showDebugLines_;
			break;
		case MenuBounds:
			if (menuRenderer_)
				menuRenderer_->showSelectionBounds_ =
					!menuRenderer_->showSelectionBounds_;
			break;
		case MenuSkipDepth:
			if (menuRenderer_)
				menuRenderer_->debugSkipDepthPrepass_ =
					!menuRenderer_->debugSkipDepthPrepass_;
			break;
		case MenuCulling:
			if (menuRenderer_)
				menuRenderer_->debugDisableCulling_ =
					!menuRenderer_->debugDisableCulling_;
			break;
		case MenuScenePane:
		case MenuMaterialPane:
		case MenuOptionsPane:
		{
			auto& pane = panes_[command - MenuScenePane];
			auto hwnd = static_cast<HWND>(BK_window_handle(pane.window));
			ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
			if (pane.floating) SetForegroundWindow(hwnd);
			SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
						 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SetFocus(hwnd);
			break;
		}
		case MenuFloatScene:
		case MenuFloatMaterial:
		case MenuFloatOptions:
			float_pane(panes_[command - MenuFloatScene]);
			break;
		case MenuDockAll:
			dock_pane(panes_[Scene], 0, 0);
			dock_pane(panes_[Material], 1, 0);
			dock_pane(panes_[Options], 1, 1);
			break;
		case MenuReset:
			reset_layout();
			break;
	}
}
