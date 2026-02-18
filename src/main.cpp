#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "app.h"
#include "config.h"
#include "logger.h"

int main(int argc, char* argv[])
{
	Logger::instance().init();
	LOG_INFO("=== vulkanwork startup ===");

	App app;

	if (argc > 1)
		app.modelPath = argv[1];
	else
		app.modelPath = std::string(MODEL_DIR) + "/DamagedHelmet.glb";

	LOG_INFO("Initial model path: %s", app.modelPath.c_str());

	try
	{
		app.run();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("Fatal exception: %s", e.what());
		return EXIT_FAILURE;
	}

	LOG_INFO("=== vulkanwork shutdown ===");
	return EXIT_SUCCESS;
}
