#define GLM_ENABLE_EXPERIMENTAL
#include "debugWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <stdexcept>

#include "gizmo.h"
#include "graphics/renderer.h"
#include "sceneGraph.h"
#include "selection.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <windowsx.h>

// Dedicated child windows keep splitter input out of both BUI controls and
// the viewport. Mouse capture lets a resize continue beyond the narrow grip.
LRESULT CALLBACK DebugWindow::splitter_proc(HWND hwnd, UINT message, WPARAM wp,
											LPARAM lp)
{
	auto* splitter =
		reinterpret_cast<Splitter*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		splitter = static_cast<Splitter*>(
			reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA,
						  reinterpret_cast<LONG_PTR>(splitter));
	}
	if (!splitter) return DefWindowProcW(hwnd, message, wp, lp);
	auto& editor = *splitter->owner;
	switch (message)
	{
		case WM_SETCURSOR:
			SetCursor(LoadCursor(nullptr, splitter->kind == SplitKind::Rows
											  ? IDC_SIZENS
											  : IDC_SIZEWE));
			return TRUE;
		case WM_PAINT:
		{
			PAINTSTRUCT paint{};
			HDC dc = BeginPaint(hwnd, &paint);
			RECT r{};
			GetClientRect(hwnd, &r);
			HBRUSH brush = CreateSolidBrush(
				splitter->dragging ? RGB(78, 125, 170) : RGB(62, 62, 62));
			FillRect(dc, &r, brush);
			DeleteObject(brush);
			EndPaint(hwnd, &paint);
			return 0;
		}
		case WM_LBUTTONDOWN:
			splitter->dragging = true;
			GetCursorPos(&splitter->start);
			splitter->initialWidth = editor.pane_width();
			if (splitter->kind == SplitKind::Columns)
			{
				int available =
					std::max(2, editor.pane_width() - 2 * SplitterSize);
				splitter->initialBoundary =
					int(available * editor.columnRatio_);
			}
			else if (splitter->kind == SplitKind::Rows)
			{
				auto& column = editor.dockColumns_[splitter->column];
				splitter->firstWeight =
					editor.panes_[column[splitter->index]].sizeWeight;
				splitter->secondWeight =
					editor.panes_[column[splitter->index + 1]].sizeWeight;
				splitter->initialBoundary =
					editor.panes_[column[splitter->index]].h;
			}
			SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_MOUSEMOVE:
			if (splitter->dragging && (wp & MK_LBUTTON))
			{
				POINT cursor{};
				GetCursorPos(&cursor);
				editor.resize_splitter(*splitter, cursor);
			}
			return 0;
		case WM_LBUTTONUP:
			if (splitter->dragging)
			{
				splitter->dragging = false;
				ReleaseCapture();
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_CAPTURECHANGED:
			splitter->dragging = false;
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONDBLCLK:
			editor.reset_splitter(*splitter);
			return 0;
	}
	return DefWindowProcW(hwnd, message, wp, lp);
}
void DebugWindow::resize_splitter(Splitter& splitter, POINT cursor)
{
	if (splitter.kind == SplitKind::Outer)
	{
		dockWidth_ = splitter.initialWidth - (cursor.x - splitter.start.x);
		dockWidth_ = pane_width();
	}
	else if (splitter.kind == SplitKind::Columns)
	{
		int available = std::max(2, pane_width() - 2 * SplitterSize);
		int minimum = std::min(160, available / 2);
		int first = std::clamp(
			splitter.initialBoundary + int(cursor.x - splitter.start.x),
			minimum, available - minimum);
		columnRatio_ = float(first) / available;
	}
	else
	{
		auto& column = dockColumns_[splitter.column];
		auto& first = panes_[column[splitter.index]];
		auto& second = panes_[column[splitter.index + 1]];
		int available = std::max(2, first.h + second.h);
		int minimum = std::min(96, available / 2);
		int height = std::clamp(
			splitter.initialBoundary + int(cursor.y - splitter.start.y),
			minimum, available - minimum);
		float sum = splitter.firstWeight + splitter.secondWeight;
		first.sizeWeight = sum * height / available;
		second.sizeWeight = sum - first.sizeWeight;
	}
	layout_panes();
}
void DebugWindow::reset_splitter(Splitter& splitter)
{
	if (splitter.kind == SplitKind::Outer)
		dockWidth_ = -1;
	else if (splitter.kind == SplitKind::Columns)
		columnRatio_ = .5f;
	else
	{
		auto& column = dockColumns_[splitter.column];
		auto& a = panes_[column[splitter.index]];
		auto& b = panes_[column[splitter.index + 1]];
		a.sizeWeight = b.sizeWeight = (a.sizeWeight + b.sizeWeight) * .5f;
	}
	layout_panes();
}

// The SDK owns widget input and painting. This subclass handles only pane
// chrome; floating keeps the same HWND/context/tree, so edits and scroll
// survive.
LRESULT CALLBACK DebugWindow::pane_proc(HWND hwnd, UINT message, WPARAM wp,
										LPARAM lp)
{
	auto* pane = static_cast<Pane*>(GetPropW(hwnd, L"VulkanWork.DockPane"));
	if (!pane) return DefWindowProcW(hwnd, message, wp, lp);
	auto& editor = *pane->owner;
	if (message == WM_KEYDOWN && editor.menu_key(wp, lp)) return 0;
	if (message == WM_SYSKEYDOWN && (GetKeyState(VK_MENU) & 0x8000) &&
		(wp == 'F' || wp == 'E' || wp == 'A' || wp == 'V' || wp == 'W'))
	{
		SendMessageW(glfwGetWin32Window(editor.parent_), WM_SYSCOMMAND,
					 SC_KEYMENU, wp);
		return 0;
	}
	auto overHeader = [&](POINT point)
	{
		int rect[4]{};
		BK_node_rect(pane->header, rect);
		return point.y >= 0 && point.y < std::max(24, rect[3]);
	};
	switch (message)
	{
		case WM_NCCALCSIZE:
		case WM_NCPAINT:
		case WM_NCACTIVATE:
			if (pane->floating) return DefWindowProcW(hwnd, message, wp, lp);
			break;
		case WM_SETCURSOR:
			if (pane->floating && LOWORD(lp) >= HTLEFT &&
				LOWORD(lp) <= HTBOTTOMRIGHT)
				return DefWindowProcW(hwnd, message, wp, lp);
			break;
		case WM_NCHITTEST:
			if (pane->floating) return DefWindowProcW(hwnd, message, wp, lp);
			break;
		case WM_GETMINMAXINFO:
			if (pane->floating)
			{
				auto result =
					CallWindowProcW(pane->originalProc, hwnd, message, wp, lp);
				auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
				limits->ptMinTrackSize = {200, 160};
				return result;
			}
			break;
		case WM_LBUTTONDOWN:
			if (overHeader({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}))
			{
				pane->dragging = true;
				GetCursorPos(&pane->dragStart);
				SetCapture(hwnd);
				return 0;
			}
			break;
		case WM_MOUSEMOVE:
			if (pane->dragging && (wp & MK_LBUTTON))
			{
				POINT cursor{};
				GetCursorPos(&cursor);
				if (std::abs(cursor.x - pane->dragStart.x) >=
						GetSystemMetrics(SM_CXDRAG) ||
					std::abs(cursor.y - pane->dragStart.y) >=
						GetSystemMetrics(SM_CYDRAG))
				{
					pane->dragging = false;
					ReleaseCapture();
					editor.float_pane(*pane);
					SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION,
								 MAKELPARAM(cursor.x, cursor.y));
				}
				return 0;
			}
			break;
		case WM_LBUTTONUP:
			if (pane->dragging)
			{
				pane->dragging = false;
				ReleaseCapture();
				return 0;
			}
			break;
		case WM_CAPTURECHANGED:
			pane->dragging = false;
			break;
		case WM_LBUTTONDBLCLK:
			if (overHeader({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}))
			{
				if (pane->floating)
					editor.dock_pane(*pane, pane->lastColumn, pane->lastIndex);
				else
					editor.float_pane(*pane);
				return 0;
			}
			break;
		case WM_NCLBUTTONDBLCLK:
			if (wp == HTCAPTION && pane->floating)
			{
				editor.dock_pane(*pane, pane->lastColumn, pane->lastIndex);
				return 0;
			}
			break;
		case WM_NCLBUTTONDOWN:
			if (pane->floating && wp >= HTLEFT && wp <= HTBOTTOMRIGHT)
				return DefWindowProcW(hwnd, message, wp, lp);
			if (wp == HTCAPTION && pane->floating)
			{
				pane->moving = true;
				auto result =
					CallWindowProcW(pane->originalProc, hwnd, message, wp, lp);
				pane->moving = false;
				editor.hide_dock_preview();
				return result;
			}
			break;
		case WM_MOVING:
			if (pane->moving)
			{
				POINT p{};
				GetCursorPos(&p);
				editor.show_dock_preview(p);
			}
			break;
		case WM_EXITSIZEMOVE:
			if (pane->moving)
			{
				POINT p{};
				GetCursorPos(&p);
				auto target = editor.dock_target(p);
				editor.hide_dock_preview();
				if (target.column >= 0)
					editor.dock_pane(*pane, target.column, target.index);
			}
			break;
		case WM_CONTEXTMENU:
		{
			POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
			if (point.x == -1 && point.y == -1) GetCursorPos(&point);
			POINT client = point;
			ScreenToClient(hwnd, &client);
			if (overHeader(client) || (pane->floating && client.y < 0))
			{
				editor.pane_menu(*pane, point);
				return 0;
			}
			break;
		}
		case WM_CLOSE:
			if (pane->floating)
			{
				editor.dock_pane(*pane, pane->lastColumn, pane->lastIndex);
				return 0;
			}
			break;
	}
	return CallWindowProcW(pane->originalProc, hwnd, message, wp, lp);
}

void DebugWindow::float_pane(Pane& pane)
{
	if (pane.floating) return;
	for (int c = 0; c < 2; ++c)
	{
		auto& column = dockColumns_[c];
		auto it = std::find(column.begin(), column.end(), pane.id);
		if (it != column.end())
		{
			pane.lastColumn = c;
			pane.lastIndex = int(it - column.begin());
			column.erase(it);
		}
	}
	HWND hwnd = static_cast<HWND>(BK_window_handle(pane.window));
	RECT rect{};
	GetWindowRect(hwnd, &rect);
	pane.floating = true;
	SetParent(hwnd, nullptr);
	SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
					  reinterpret_cast<LONG_PTR>(glfwGetWin32Window(parent_)));
	SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top,
				 std::max(340L, rect.right - rect.left),
				 std::max(360L, rect.bottom - rect.top),
				 SWP_FRAMECHANGED | SWP_NOACTIVATE);
	pane.x = pane.y = pane.w = pane.h = -1;
	layout_panes();
}

void DebugWindow::dock_pane(Pane& pane, int column, int index)
{
	hide_dock_preview();
	column = std::clamp(column, 0, 1);
	for (auto& items : dockColumns_)
		items.erase(std::remove(items.begin(), items.end(), pane.id),
					items.end());
	auto& items = dockColumns_[column];
	index = std::clamp(index, 0, int(items.size()));
	items.insert(items.begin() + index, pane.id);
	pane.lastColumn = column;
	pane.lastIndex = index;
	HWND hwnd = static_cast<HWND>(BK_window_handle(pane.window));
	pane.floating = false;
	SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
	SetWindowLongPtrW(hwnd, GWL_STYLE,
					  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER);
	SetParent(hwnd, glfwGetWin32Window(parent_));
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					 SWP_NOACTIVATE);
	pane.x = pane.y = pane.w = pane.h = -1;
	layout_panes();
}

DebugWindow::DockTarget DebugWindow::dock_target(POINT cursor) const
{
	HWND parent = glfwGetWin32Window(parent_);
	POINT client = cursor;
	ScreenToClient(parent, &client);
	RECT bounds{};
	GetClientRect(parent, &bounds);
	int width = pane_width();
	if (!width) width = std::min(720L, std::max(2L, bounds.right / 2));
	if (client.x < bounds.right - width || client.x >= bounds.right ||
		client.y < 0 || client.y >= bounds.bottom)
		return {};
	int available = std::max(2, width - 2 * SplitterSize);
	int minimum = std::min(160, available / 2);
	int leftWidth = SplitterSize +
					std::clamp(int(available * columnRatio_), minimum,
							   available - minimum) +
					SplitterSize / 2;
	int column = client.x < bounds.right - width + leftWidth ? 0 : 1;
	int count = int(dockColumns_[column].size());
	int index = 0;
	for (auto id : dockColumns_[column])
	{
		const auto& item = panes_[id];
		if (client.y > item.y + item.h / 2) ++index;
	}
	int top = bounds.bottom * index / (count + 1);
	int bottom = bounds.bottom * (index + 1) / (count + 1);
	POINT a{
		column == 0 ? bounds.right - width : bounds.right - width + leftWidth,
		top};
	POINT b{column == 0 ? bounds.right - width + leftWidth : bounds.right,
			bottom};
	ClientToScreen(parent, &a);
	ClientToScreen(parent, &b);
	return {column, index, {a.x, a.y, b.x, b.y}};
}
void DebugWindow::show_dock_preview(POINT cursor)
{
	auto target = dock_target(cursor);
	if (target.column < 0)
	{
		hide_dock_preview();
		return;
	}
	const auto& r = target.rect;
	SetWindowPos(dockPreview_, HWND_TOPMOST, r.left, r.top, r.right - r.left,
				 r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
void DebugWindow::hide_dock_preview()
{
	if (dockPreview_) ShowWindow(dockPreview_, SW_HIDE);
}
void DebugWindow::pane_menu(Pane& pane, POINT point)
{
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, pane.floating ? 2 : 1,
				pane.floating ? L"Dock back" : L"Float pane");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, 3, L"Dock first column - top");
	AppendMenuW(menu, MF_STRING, 4, L"Dock first column - bottom");
	AppendMenuW(menu, MF_STRING, 5, L"Dock second column - top");
	AppendMenuW(menu, MF_STRING, 6, L"Dock second column - bottom");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, 7, L"Reset pane layout");
	int command = TrackPopupMenu(
		menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
		static_cast<HWND>(BK_window_handle(pane.window)), nullptr);
	DestroyMenu(menu);
	if (command == 1)
		float_pane(pane);
	else if (command == 2)
		dock_pane(pane, pane.lastColumn, pane.lastIndex);
	else if (command >= 3 && command <= 6)
	{
		int column = command >= 5 ? 1 : 0;
		dock_pane(pane, column,
				  command % 2 ? 0 : int(dockColumns_[column].size()));
	}
	else if (command == 7)
	{
		reset_layout();
	}
}
void DebugWindow::layout_panes()
{
	int w, h;
	glfwGetWindowSize(parent_, &w, &h);
	int width = pane_width();
	int count = int(!dockColumns_[0].empty()) + int(!dockColumns_[1].empty());
	int used = 0;
	auto grip =
		[&](SplitKind kind, int column, int index, int x, int y, int gw, int gh)
	{
		auto& splitter = splitters_[used++];
		splitter.kind = kind;
		splitter.column = column;
		splitter.index = index;
		SetWindowPos(splitter.window, HWND_TOP, x, y, std::max(1, gw),
					 std::max(1, gh), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	};
	if (count) grip(SplitKind::Outer, 0, 0, w - width, 0, SplitterSize, h);
	int available =
		std::max(2, width - SplitterSize - (count == 2 ? SplitterSize : 0));
	int minimum = std::min(160, available / 2);
	int firstWidth =
		std::clamp(int(available * columnRatio_), minimum, available - minimum);
	int x = w - width + SplitterSize;
	int populated = 0;
	for (int c = 0; c < 2; ++c)
	{
		auto& column = dockColumns_[c];
		if (column.empty()) continue;
		int cw = count == 1		  ? available
				 : populated == 0 ? firstWidth
								  : available - firstWidth;
		int rowSpace = std::max(int(column.size()),
								h - SplitterSize * (int(column.size()) - 1));
		float total = 0;
		for (auto id : column) total += panes_[id].sizeWeight;
		int y = 0;
		float accumulated = 0;
		for (int i = 0; i < int(column.size()); ++i)
		{
			auto& pane = panes_[column[i]];
			accumulated += pane.sizeWeight;
			int bottom =
				i + 1 == int(column.size())
					? h
					: int(rowSpace * accumulated / total) + i * SplitterSize;
			int ph = std::max(1, bottom - y);
			if (pane.x != x || pane.y != y || pane.w != cw || pane.h != ph)
			{
				SetWindowPos(static_cast<HWND>(BK_window_handle(pane.window)),
							 HWND_TOP, x, y, cw, ph, SWP_NOACTIVATE);
				pane.x = x;
				pane.y = y;
				pane.w = cw;
				pane.h = ph;
			}
			y = bottom;
			if (i + 1 < int(column.size()))
			{
				grip(SplitKind::Rows, c, i, x, y, cw, SplitterSize);
				y += SplitterSize;
			}
		}
		x += cw;
		++populated;
		if (populated < count)
		{
			grip(SplitKind::Columns, 0, 0, x, 0, SplitterSize, h);
			x += SplitterSize;
		}
	}
	for (; used < int(splitters_.size()); ++used)
		ShowWindow(splitters_[used].window, SW_HIDE);
}

BKNode* DebugWindow::node(BKNode* parent, const char* type, const char* label)
{
	auto* n = BK_node_new(type);
	BK_set_str(n, "label", label);
	if (!std::strcmp(type, "label") || !std::strcmp(type, "button"))
		BK_set_str(n, "text", label);
	if (parent) BK_node_add(parent, n, -1);
	return n;
}
BKNode* DebugWindow::section(const char* title, PaneId pane)
{
	auto* p = node(panes_[pane].body, "box");
	auto* column = node(p, "column");
	node(column, "label", title);
	return column;
}
void DebugWindow::bind(BKNode* n, std::function<void(BKNode*, int)> edit,
					   std::function<void(BKNode*)> sync)
{
	auto* root = n;
	while (BK_node_parent(root)) root = BK_node_parent(root);
	BKContext* context = nullptr;
	for (auto& pane : panes_)
		if (pane.root == root) context = pane.context;
	auto b = std::make_unique<Binding>(
		Binding{n, context, std::move(edit), std::move(sync)});
	BK_node_set_ref(n, b.get());
	if (b->sync) b->sync(n);
	bindings_.push_back(std::move(b));
}
void DebugWindow::button(BKNode* p, const char* title, std::function<void()> fn)
{
	bind(node(p, "button", title),
		 [this, fn](BKNode*, int kind)
		 {
			 if (kind == BK_EV_CLICK) actions_.push_back(fn);
		 });
}
void DebugWindow::check(BKNode* p, const char* title, bool& value)
{
	bind(
		node(p, "checkbox", title),
		[&value](BKNode* n, int k)
		{
			if (k == BK_EV_CHANGE) value = BK_get_num(n, "value") != 0;
		},
		[&value](BKNode* n) { BK_set_num(n, "value", value); });
}
void DebugWindow::number(BKNode* p, const char* title, float& value, float lo,
						 float hi, std::function<void()> changed)
{
	auto* n = node(p, "number", title);
	BK_set_num(n, "min", lo);
	BK_set_num(n, "max", hi);
	BK_set_num(n, "step", 0.05);
	bind(
		n,
		[&value, lo, hi, changed](BKNode* n, int k)
		{
			if (k != BK_EV_CHANGE) return;
			value = std::clamp(float(BK_get_num(n, "value")), lo, hi);
			if (changed) changed();
		},
		[&value](BKNode* n) { BK_set_num(n, "value", value); });
}
void DebugWindow::choice(BKNode* p, const char* title,
						 const std::vector<const char*>& items,
						 std::function<int()> get, std::function<void(int)> set)
{
	auto* n = node(p, "dropdown", title);
	BK_set_strs(n, "items", items.data(), int(items.size()));
	bind(
		n,
		[set, count = int(items.size())](BKNode* n, int k)
		{
			if (k == BK_EV_CHANGE)
				set(std::clamp(int(BK_get_num(n, "value")), 0, count - 1));
		},
		[get](BKNode* n) { BK_set_num(n, "value", get()); });
}
void DebugWindow::color(BKNode* p, const char* title, float* value, int count,
						std::function<void()> changed)
{
	auto* n = node(p, "swatch", title);
	bind(
		n,
		[value, count, changed](BKNode* n, int k)
		{
			if (k != BK_EV_CHANGE) return;
			float rgba[4];
			BK_get_color(n, "color", rgba);
			std::copy_n(rgba, count, value);
			if (changed) changed();
		},
		[value, count](BKNode* n)
		{
			float rgba[4] = {value[0], value[1], value[2],
							 count == 4 ? value[3] : 1.f};
			BK_set_color(n, "color", rgba);
		});
}
void DebugWindow::init(PlatformWindow* parent)
{
	if (BK_version() != BUI_KIT_VERSION || !BK_window_supported())
		throw std::runtime_error(
			"A matching BUI 0.5.0 native window SDK is required");
	parent_ = parent;
	init_menu();
	WNDCLASSW splitterClass{};
	splitterClass.style = CS_DBLCLKS;
	splitterClass.lpfnWndProc = &DebugWindow::splitter_proc;
	splitterClass.hInstance = GetModuleHandleW(nullptr);
	splitterClass.lpszClassName = L"VulkanWork.PaneSplitter";
	RegisterClassW(&splitterClass);
	for (auto& splitter : splitters_)
	{
		splitter.owner = this;
		splitter.window = CreateWindowExW(
			0, splitterClass.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0,
			0, 1, 1, glfwGetWin32Window(parent), nullptr,
			splitterClass.hInstance, &splitter);
		if (!splitter.window)
			throw std::runtime_error("Could not create pane splitters");
	}

	dockColumns_[0] = {Scene};
	dockColumns_[1] = {Material, Options};
	dockPreview_ = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
		L"STATIC", L"", WS_POPUP | SS_WHITERECT, 0, 0, 0, 0,
		glfwGetWin32Window(parent), nullptr, GetModuleHandleW(nullptr),
		nullptr);
	if (dockPreview_)
		SetLayeredWindowAttributes(dockPreview_, 0, 100, LWA_ALPHA);
	const char* titles[] = {"Scene / Object", "Material", "Options"};
	for (int i = 0; i < PaneCount; ++i)
	{
		auto& pane = panes_[i];
		pane.owner = this;
		pane.id = PaneId(i);
		pane.lastColumn = i == Scene ? 0 : 1;
		pane.lastIndex = i == Options ? 1 : 0;
		pane.root = BK_node_new("scroll");
		pane.body = node(pane.root, "column");
		pane.context = BK_context_new(pane.root);
		pane.header = BK_node_new("column");
		node(pane.header, "label", titles[i]);
		node(pane.header, "label", "Drag to move / right-click for docking");
		node(pane.header, "separator");
		BK_context_set_header(pane.context, pane.header);
		BKHost host{};
		host.emit = [](void*, BKNode* n, int kind, void*, double, double)
		{
			auto* b = static_cast<Binding*>(BK_node_ref(n));
			if (b && b->edit) b->edit(n, kind);
		};
		BK_context_set_host(pane.context, &host);
		pane.window = BK_window_new(pane.context, titles[i], 320, 360);
		if (!pane.window)
			throw std::runtime_error("Could not create a BUI editor pane");
		HWND child = static_cast<HWND>(BK_window_handle(pane.window));
		SetWindowLongPtrW(child, GWL_STYLE,
						  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER);
		SetParent(child, glfwGetWin32Window(parent));
		SetPropW(child, L"VulkanWork.DockPane", &pane);
		pane.originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
			child, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(&DebugWindow::pane_proc)));
		BK_window_show(pane.window);
	}
}
void DebugWindow::clear_body()
{
	for (auto& pane : panes_)
	{
		if (!pane.context) continue;
		BK_focus_node(pane.context, nullptr);
		BK_close_popups(pane.context);
		while (BK_node_child_count(pane.body))
		{
			auto* child = BK_node_child(pane.body, 0);
			BK_node_remove(pane.body, child);
			BK_node_free(child);
		}
	}
	stats_ = nullptr;
	bindings_.clear();
	for (auto* item : treeItems_) BK_tree_item_free(item);
	treeItems_.clear();
}
void DebugWindow::cleanup()
{
	HWND parent = glfwGetWin32Window(parent_);
	if (mainProc_)
		SetWindowLongPtrW(parent, GWLP_WNDPROC,
						  reinterpret_cast<LONG_PTR>(mainProc_));
	RemovePropW(parent, L"VulkanWork.EditorMenu");
	SetMenu(parent, nullptr);
	if (menuBar_) DestroyMenu(menuBar_);
	menuBar_ = nullptr;
	mainProc_ = nullptr;

	for (auto& pane : panes_)
	{
		if (pane.window)
		{
			HWND hwnd = static_cast<HWND>(BK_window_handle(pane.window));
			if (pane.originalProc)
				SetWindowLongPtrW(
					hwnd, GWLP_WNDPROC,
					reinterpret_cast<LONG_PTR>(pane.originalProc));
			RemovePropW(hwnd, L"VulkanWork.DockPane");
			BK_window_free(pane.window);
		}
		if (pane.context) BK_context_free(pane.context);
		// Contexts borrow their roots and headers.
		if (pane.root) BK_node_free(pane.root);
		if (pane.header) BK_node_free(pane.header);
		pane = {};
	}
	for (auto& splitter : splitters_)
	{
		if (splitter.window) DestroyWindow(splitter.window);
		splitter.window = nullptr;
	}
	if (dockPreview_) DestroyWindow(dockPreview_);
	dockPreview_ = nullptr;
	for (auto& column : dockColumns_) column.clear();
	bindings_.clear();
	actions_.clear();
	for (auto* item : treeItems_) BK_tree_item_free(item);
	treeItems_.clear();
}

int DebugWindow::pane_width() const
{
	int w, h;
	glfwGetWindowSize(parent_, &w, &h);
	int columns = int(!dockColumns_[0].empty()) + int(!dockColumns_[1].empty());
	if (!columns) return 0;
	int initial = columns == 2 ? std::min(720, std::max(2, w / 2))
							   : std::min(380, std::max(1, w / 3));
	int maximum = std::max(1, w - 160);
	int minimum = std::min(
		maximum, columns * 160 + (columns == 2 ? 2 : 1) * SplitterSize);
	return std::clamp(dockWidth_ >= 0 ? dockWidth_ : initial, minimum, maximum);
}
bool DebugWindow::captures_mouse() const
{
	double x, y;
	glfwGetCursorPos(parent_, &x, &y);
	int w, h;
	glfwGetWindowSize(parent_, &w, &h);
	if (menuActive_) return true;
	if (pane_width() && x >= w - pane_width()) return true;
	for (const auto& pane : panes_)
		if (BK_modal_active(pane.context) || BK_popup_count(pane.context))
			return true;
	return false;
}
bool DebugWindow::captures_keyboard() const
{
	if (menuActive_ || (menuEscape_ && (GetKeyState(VK_ESCAPE) & 0x8000)))
		return true;
	if (GetFocus() != glfwGetWin32Window(parent_)) return true;
	for (const auto& pane : panes_)
		if (BK_modal_active(pane.context)) return true;
	return false;
}

void DebugWindow::rebuild(Renderer& renderer, LightEnvironment& lights,
						  Selection& selection, Gizmo& gizmo, SceneGraph& graph)
{
	clear_body();
	auto* info = section("Frame Statistics", Options);
	stats_ = node(info, "label");
	node(info, "label", renderer.gpu_name());
	check(info, "Tile heatmap", renderer.showHeatmap_);
	check(info, "Light wireframes", renderer.showDebugLines_);
	check(info, "Selection bounds", renderer.showSelectionBounds_);
	auto* debug = section("Depth / Rasterizer", Options);
	check(debug, "Skip depth prepass", renderer.debugSkipDepthPrepass_);
	check(debug, "Disable backface culling", renderer.debugDisableCulling_);
	choice(
		debug, "Front face", {"CCW", "CW"},
		[&renderer] { return renderer.debugFrontFace_; },
		[&renderer](int i) { renderer.debugFrontFace_ = i; });
	auto* light = section("Environment", Options);
	color(light, "Ambient color", &lights.ambient.color.x, 3);
	number(light, "Ambient intensity", lights.ambient.intensity, 0, 1);
	auto* hierarchy = section("Scene Hierarchy");
	auto* tree = node(hierarchy, "tree");
	BK_set_num(tree, "height", 200);
	for (auto& n : graph.nodes)
		treeItems_.push_back(BK_tree_item_new(n.name.c_str(), 0));
	for (size_t i = 0; i < graph.nodes.size(); ++i)
		for (auto child : graph.nodes[i].children)
			if (child < treeItems_.size())
				BK_tree_item_add(treeItems_[i], treeItems_[child]);
	std::vector<BKTreeItem*> roots;
	for (auto i : graph.roots)
		if (i < treeItems_.size()) roots.push_back(treeItems_[i]);
	BK_tree_set_roots(tree, roots.data(), int(roots.size()));
	if (selection.selectedNode && *selection.selectedNode < treeItems_.size())
	{
		auto i = *selection.selectedNode;
		for (auto p = graph.nodes[i].parent; p && *p < graph.nodes.size();
			 p = graph.nodes[*p].parent)
			BK_tree_item_set(treeItems_[*p], "open", 1);
		BK_tree_select(tree, treeItems_[i]);
	}
	bind(tree,
		 [this, &selection](BKNode* n, int k)
		 {
			 if (k != BK_EV_CHANGE && k != BK_EV_ACTIVATE) return;
			 auto* selected = BK_tree_selected(n);
			 auto it =
				 std::find(treeItems_.begin(), treeItems_.end(), selected);
			 if (it != treeItems_.end())
				 selection.selectedNode = uint32_t(it - treeItems_.begin());
		 });
	auto* row = node(hierarchy, "row");
	button(row, "Delete", [this] { deleteRequested = true; });
	button(row, "Deselect", [&selection] { selection.selectedNode.reset(); });
	auto* materialInfo = section("Selected Material", Material);
	if (!selection.selectedNode ||
		*selection.selectedNode >= graph.nodes.size())
	{
		node(materialInfo, "label", "Select a mesh to edit its material.");
		node(panes_[Scene].body, "label",
			 "Select an object to edit its properties.");
		return;
	}
	auto idx = *selection.selectedNode;
	auto& object = graph.nodes[idx];
	node(materialInfo, "label", object.name.c_str());
	if (!object.meshIndex)
		node(materialInfo, "label", "This object has no mesh material.");
	auto* inspector = section("Object Editor");
	auto* name = node(inspector, "text", "Name");
	bind(
		name,
		[&object, this, idx](BKNode* n, int k)
		{
			if (k == BK_EV_CHANGE || k == BK_EV_SUBMIT)
			{
				const char* text = BK_get_str(n, "value");
				if (text && *text)
				{
					object.name = text;
					BK_tree_item_set_label(treeItems_[idx], text);
				}
			}
		},
		[&object](BKNode* n) { BK_set_str(n, "value", object.name.c_str()); });
	choice(
		inspector, "Operation (W/E/R)", {"Translate", "Rotate", "Scale"},
		[&gizmo] { return int(gizmo.operation); },
		[&gizmo](int i) { gizmo.operation = Gizmo::Op(i); });
	choice(
		inspector, "Space", {"World", "Local"},
		[&gizmo] { return int(gizmo.space); },
		[&gizmo](int i) { gizmo.space = Gizmo::Space(i); });
	check(inspector, "Snap", gizmo.useSnap);
	number(inspector, "Translation snap", gizmo.snapTranslate, .001f, 100);
	number(inspector, "Rotation snap", gizmo.snapRotate, .1f, 180);
	number(inspector, "Scale snap", gizmo.snapScale, .001f, 10);
	// Derive from the current matrix on every edit: viewport manipulation and
	// numeric edits can be interleaved without stale cached components.
	for (int component = 0; component < 3; ++component)
	{
		const char* names[] = {"Position", "Rotation", "Scale"};
		auto* group = node(inspector, "column", names[component]);
		node(group, "label", names[component]);
		row = node(group, "row");
		for (int axis = 0; axis < 3; ++axis)
		{
			auto* n = node(row, "number",
						   axis == 0   ? "X"
						   : axis == 1 ? "Y"
									   : "Z");
			BK_set_num(n, "min", -100000);
			BK_set_num(n, "max", 100000);
			BK_set_num(n, "step", component == 1 ? .5 : .05);
			auto values = [&object](glm::vec3& t, glm::vec3& r, glm::vec3& s)
			{
				glm::quat q;
				glm::vec3 skew;
				glm::vec4 perspective;
				glm::decompose(object.localTransform, s, q, t, skew,
							   perspective);
				r = glm::degrees(glm::eulerAngles(q));
			};
			bind(
				n,
				[&object, component, axis, values](BKNode* n, int k)
				{
					if (k != BK_EV_CHANGE) return;
					glm::vec3 t, r, s;
					values(t, r, s);
					(component == 0	  ? t
					 : component == 1 ? r
									  : s)[axis] =
						float(BK_get_num(n, "value"));
					object.localTransform =
						glm::translate(glm::mat4(1), t) *
						glm::mat4_cast(glm::quat(glm::radians(r))) *
						glm::scale(glm::mat4(1), s);
				},
				[component, axis, values](BKNode* n)
				{
					glm::vec3 t, r, s;
					values(t, r, s);
					BK_set_num(n, "value",
							   (component == 0	 ? t
								: component == 1 ? r
												 : s)[axis]);
				});
		}
	}
	if (object.light)
	{
		auto& l = *object.light;
		auto* p = section("Light");
		choice(
			p, "Type", {"Directional", "Point", "Spot"}, [&l]
			{ return int(l.type); }, [&l](int i) { l.type = LightType(i); });
		color(p, "Color", &l.color.x, 3);
		number(p, "Intensity", l.intensity, 0, 1000);
		number(p, "Radius", l.radius, .1f, 500);
		number(p, "Inner cone (radians)", l.innerConeAngle, .01f, 1.55f,
			   [&l]
			   {
				   l.outerConeAngle =
					   std::max(l.outerConeAngle, l.innerConeAngle);
			   });
		number(p, "Outer cone (radians)", l.outerConeAngle, .01f, 1.55f,
			   [&l]
			   {
				   l.innerConeAngle =
					   std::min(l.innerConeAngle, l.outerConeAngle);
			   });
	}
	if (object.splat)
	{
		auto& s = *object.splat;
		auto* p = section("Gaussian Splat");
		node(p, "label", s.path.c_str());
		choice(
			p, "Mode", {"Gaussian", "Points"}, [&s] { return int(s.mode); },
			[&s](int i) { s.mode = SplatRenderMode(i); });
		number(p, "Point size", s.pointSize, 1, 16);
	}
	if (object.meshIndex && *object.meshIndex < renderer.meshes().size())
	{
		uint32_t mi = renderer.meshes()[*object.meshIndex].materialIndex;
		if (mi >= renderer.materials().size()) return;
		auto& m = renderer.materials()[mi];
		auto changed = [&renderer, &m, mi]
		{
			m.edited = true;
			renderer.update_material(mi);
		};
		auto* p = section("PBR Material", Material);
		color(p, "Base color", &m.baseColorFactor.x, 4, changed);
		number(p, "Metallic", m.metallicFactor, 0, 1, changed);
		number(p, "Roughness", m.roughnessFactor, 0, 1, changed);
		color(p, "Emissive", &m.emissiveFactor.x, 3, changed);
		number(p, "Normal strength", m.normalScale, 0, 2, changed);
		number(p, "Occlusion", m.occlusionStrength, 0, 1, changed);
		choice(
			p, "Alpha mode", {"Opaque", "Mask", "Blend"},
			[&m] { return int(m.alphaMode); },
			[&m, changed](int i)
			{
				m.alphaMode = AlphaMode(i);
				changed();
			});
		number(p, "Alpha cutoff", m.alphaCutoff, 0, 1, changed);
		const char* labels[] = {"Base Color", "Metal/Rough", "Normal",
								"Occlusion", "Emissive"};
		for (int i = 0; i < 5; ++i)
		{
			auto slot = TextureSlot(i);
			auto* box = node(p, "box");
			node(box, "label", labels[i]);
			auto* image = node(box, "image");
			BK_set_num(image, "width", 64);
			BK_set_num(image, "height", 64);
			int32_t ti = m.texture(slot);
			if (ti >= 0 && size_t(ti) < renderer.textures().size())
			{
				const auto& tex = renderer.textures()[ti];
				if (!tex.pixels.empty())
					BK_node_set_image(image, tex.pixels.data(), tex.width,
									  tex.height);
				auto* path = node(box, "text", "Source");
				BK_set_str(path, "value",
						   (tex.sourcePath.empty() ? tex.containerPath
												   : tex.sourcePath)
							   .c_str());
				BK_set_num(path, "enabled", 0);
			}
			row = node(box, "row");
			button(row, "Browse...",
				   [this, mi, slot] { textureBrowseRequested = {mi, slot}; });
			button(row, "Clear",
				   [&renderer, mi, slot, this]
				   {
					   renderer.set_material_texture(mi, slot, {});
					   nodeCount_ = size_t(-1);
				   });
		}
		node(p, "label", textureError.c_str());
	}
}
void DebugWindow::draw(Renderer& r, LightEnvironment& l, Selection& s, Gizmo& g,
					   SceneGraph& graph)
{
	menuRenderer_ = &r;
	menuScene_ = &graph;
	menuSelection_ = &s;
	menuGizmo_ = &g;
	if (!(GetKeyState(VK_ESCAPE) & 0x8000)) menuEscape_ = false;

	// Structural changes run outside BUI's input dispatch, before bindings
	// are rebuilt. A light insertion can reallocate the scene's node vector.
	auto actions = std::move(actions_);
	actions_.clear();
	for (auto& action : actions) action();
	layout_panes();
	int lt = s.selectedNode && *s.selectedNode < graph.nodes.size() &&
					 graph.nodes[*s.selectedNode].light
				 ? int(graph.nodes[*s.selectedNode].light->type)
				 : -1;
	if (nodeCount_ != graph.nodes.size() || selected_ != s.selectedNode ||
		materialCount_ != r.materials().size() ||
		textureCount_ != r.textures().size() || lt != lightType_)
	{
		rebuild(r, l, s, g, graph);
		nodeCount_ = graph.nodes.size();
		selected_ = s.selectedNode;
		materialCount_ = r.materials().size();
		textureCount_ = r.textures().size();
		lightType_ = lt;
	}
	for (auto& b : bindings_)
		if (b->sync && BK_focus(b->context) != b->node) b->sync(b->node);
	double now = platform::time_seconds();
	++statsFrames_;
	double dt = now - statsTime_;
	if (dt >= .25 || BK_get_str(stats_, "text")[0] == '\0')
	{
		char text[200];
		auto e = r.viewport_extent();
		std::snprintf(text, sizeof(text),
					  "%.1f FPS | %.2f ms | %u x %u | %u lights",
					  dt > 0 ? statsFrames_ / dt : 0, dt * 1000 / statsFrames_,
					  e.width, e.height, l.total_light_count());
		BK_set_str(stats_, "text", text);
		statsFrames_ = 0;
		statsTime_ = now;
	}
	for (auto& pane : panes_)
		if (BK_needs_render(pane.context) || BK_animating(pane.context))
			BK_window_render_now(pane.window);
}
