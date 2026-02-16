#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "app.h"
#include "config.h"

int main(int argc, char* argv[])
{
	App app;

	if (argc > 1)
		app.modelPath = argv[1];
	else
		app.modelPath = std::string(MODEL_DIR) + "/DamagedHelmet.glb";

	try
	{
		app.run();
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "Fatal error: %s\n", e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
