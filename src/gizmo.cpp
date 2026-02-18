#include "gizmo.h"

#include <ImGuizmo.h>
#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>

void Gizmo::begin_frame()
{
	ImGuizmo::BeginFrame();
	ImGuizmo::SetOrthographic(false);
}

bool Gizmo::manipulate(const glm::mat4& view, const glm::mat4& proj,
					   glm::mat4& objectMatrix, float viewportX,
					   float viewportY, float viewportW, float viewportH)
{
	ImGuizmo::SetRect(viewportX, viewportY, viewportW, viewportH);

	ImGuizmo::OPERATION op;
	switch (operation)
	{
		case Op::Translate:
			op = ImGuizmo::TRANSLATE;
			break;
		case Op::Rotate:
			op = ImGuizmo::ROTATE;
			break;
		case Op::Scale:
			op = ImGuizmo::SCALE;
			break;
	}

	ImGuizmo::MODE mode =
		(space == Space::Local) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	float snapValues[3] = {0.0f, 0.0f, 0.0f};
	if (useSnap)
	{
		switch (operation)
		{
			case Op::Translate:
				snapValues[0] = snapValues[1] = snapValues[2] = snapTranslate;
				break;
			case Op::Rotate:
				snapValues[0] = snapValues[1] = snapValues[2] = snapRotate;
				break;
			case Op::Scale:
				snapValues[0] = snapValues[1] = snapValues[2] = snapScale;
				break;
		}
	}

	return ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
								mode, glm::value_ptr(objectMatrix), nullptr,
								useSnap ? snapValues : nullptr);
}

bool Gizmo::is_using() const { return ImGuizmo::IsUsing(); }
