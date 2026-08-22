// Force Github Linguist Refresh
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "app.h"
#include "config.h"
#include "logger.h"
#include "platform/platform.h"

static int vulkanwork_main(int argc, char* argv[])
{
	Logger::instance().init();
	LOG_INFO("=== vulkanwork startup ===");

	App app;

	if (argc > 1)
		app.modelPath = argv[1];
	else
		app.modelPath = platform::model_path("DamagedHelmet.glb");

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

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(android_app* state)
{
	app_dummy();
	platform::set_android_app(state);

	try
	{
		platform::prepare_android_assets();
	}
	catch (const std::exception& e)
	{
		Logger::instance().init();
		LOG_ERROR("Failed to prepare Android assets: %s", e.what());
		return;
	}

	char arg0[] = "vulkanwork";
	char* argv[] = {arg0};
	(void)vulkanwork_main(1, argv);
}
#else
int main(int argc, char* argv[]) { return vulkanwork_main(argc, argv); }
#endif
