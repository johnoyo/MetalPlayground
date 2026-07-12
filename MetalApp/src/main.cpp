#include "MetalEngine.h"

int main()
{
	MTLE::MetalEngine* mtlEngine = new MTLE::MetalEngine;

	mtlEngine->Init();
	mtlEngine->Run();
	mtlEngine->Clean();

	delete mtlEngine;

	return 0;
}
