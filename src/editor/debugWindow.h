#pragma once
#include <BUI_kit.h>
#include <windows.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "graphics/material.h"
#include "platform/platform.h"

struct Renderer;
struct LightEnvironment;
struct Selection;
struct Gizmo;
struct SceneGraph;

// Retained BUI trees hosted in dockable native panes. Bindings are owned for
// as long as their nodes; edits are applied on the application thread.
struct DebugWindow
{
	void init(PlatformWindow* parent);
	void cleanup();
	void invalidate()
	{
		clear_body();
		nodeCount_ = size_t(-1);
	}
	void draw(Renderer&, LightEnvironment&, Selection&, Gizmo&, SceneGraph&);
	bool captures_mouse() const;
	bool captures_keyboard() const;
	int pane_width() const;
	std::function<void()> newScene, loadScene, saveScene, saveAs, exit;
	bool importRequested = false, importSplatRequested = false,
		 deleteRequested = false;
	std::optional<std::pair<uint32_t, TextureSlot>> textureBrowseRequested;
	std::string textureError;

   private:
	struct Binding
	{
		BKNode* node;
		BKContext* context;
		std::function<void(BKNode*, int)> edit;
		std::function<void(BKNode*)> sync;
	};
	PlatformWindow* parent_ = nullptr;
	HMENU menuBar_ = nullptr;
	WNDPROC mainProc_ = nullptr;
	bool menuActive_ = false, menuEscape_ = false;
	Renderer* menuRenderer_ = nullptr;
	SceneGraph* menuScene_ = nullptr;
	Selection* menuSelection_ = nullptr;
	Gizmo* menuGizmo_ = nullptr;
	enum MenuCommand : UINT
	{
		MenuNew = 40000,
		MenuOpen,
		MenuSave,
		MenuSaveAs,
		MenuImportMesh,
		MenuImportSplat,
		MenuExit,
		MenuDelete,
		MenuDeselect,
		MenuTranslate,
		MenuRotate,
		MenuScale,
		MenuWorld,
		MenuLocal,
		MenuSnap,
		MenuDirectional,
		MenuPoint,
		MenuSpot,
		MenuHeatmap,
		MenuWireframes,
		MenuBounds,
		MenuSkipDepth,
		MenuCulling,
		MenuScenePane,
		MenuMaterialPane,
		MenuOptionsPane,
		MenuFloatScene,
		MenuFloatMaterial,
		MenuFloatOptions,
		MenuDockAll,
		MenuReset
	};
	static LRESULT CALLBACK main_proc(HWND, UINT, WPARAM, LPARAM);
	void init_menu();
	void update_menu();
	bool menu_key(WPARAM, LPARAM);
	void queue_menu_command(UINT);
	void execute_menu_command(UINT);
	void reset_layout();
	enum PaneId
	{
		Scene,
		Material,
		Options,
		PaneCount
	};
	struct Pane
	{
		DebugWindow* owner = nullptr;
		PaneId id = Scene;
		WNDPROC originalProc = nullptr;
		bool floating = false, dragging = false, moving = false;
		POINT dragStart{};
		int lastColumn = 0, lastIndex = 0;
		float sizeWeight = 1.f;
		BKWindow* window = nullptr;
		BKContext* context = nullptr;
		BKNode* root = nullptr;
		BKNode* body = nullptr;
		BKNode* header = nullptr;
		int x = -1, y = -1, w = -1, h = -1;
	};
	std::array<Pane, PaneCount> panes_{};
	std::array<std::vector<PaneId>, 2> dockColumns_;
	HWND dockPreview_ = nullptr;
	static constexpr int SplitterSize = 6;
	enum class SplitKind
	{
		Outer,
		Columns,
		Rows
	};
	struct Splitter
	{
		DebugWindow* owner = nullptr;
		HWND window = nullptr;
		SplitKind kind = SplitKind::Outer;
		int column = 0, index = 0;
		bool dragging = false;
		POINT start{};
		int initialWidth = 0, initialBoundary = 0;
		float firstWeight = 0, secondWeight = 0;
	};
	std::array<Splitter, PaneCount + 2> splitters_{};
	int dockWidth_ = -1;
	float columnRatio_ = .5f;
	static LRESULT CALLBACK splitter_proc(HWND, UINT, WPARAM, LPARAM);
	void resize_splitter(Splitter&, POINT);
	void reset_splitter(Splitter&);
	struct DockTarget
	{
		int column = -1, index = 0;
		RECT rect{};
	};
	static LRESULT CALLBACK pane_proc(HWND, UINT, WPARAM, LPARAM);
	void float_pane(Pane&);
	void dock_pane(Pane&, int column, int index);
	DockTarget dock_target(POINT) const;
	void show_dock_preview(POINT);
	void hide_dock_preview();
	void pane_menu(Pane&, POINT);
	void layout_panes();
	BKNode* stats_ = nullptr;
	std::vector<std::unique_ptr<Binding>> bindings_;
	std::vector<std::function<void()>> actions_;
	std::vector<BKTreeItem*> treeItems_;
	std::optional<uint32_t> selected_;
	size_t nodeCount_ = size_t(-1), materialCount_ = 0, textureCount_ = 0;
	int lightType_ = -1;
	double statsTime_ = 0;
	unsigned statsFrames_ = 0;
	BKNode* node(BKNode*, const char*, const char* = "");
	BKNode* section(const char*, PaneId pane = Scene);
	void bind(BKNode*, std::function<void(BKNode*, int)>,
			  std::function<void(BKNode*)> = {});
	void button(BKNode*, const char*, std::function<void()>);
	void check(BKNode*, const char*, bool&);
	void number(BKNode*, const char*, float&, float, float,
				std::function<void()> = {});
	void choice(BKNode*, const char*, const std::vector<const char*>&,
				std::function<int()>, std::function<void(int)>);
	void color(BKNode*, const char*, float*, int, std::function<void()> = {});
	void rebuild(Renderer&, LightEnvironment&, Selection&, Gizmo&, SceneGraph&);
	void clear_body();
};
