#include "client/Application.h"
#include <SDL3/SDL_main.h>
#include <iostream>
int main(int argc,char** argv) {
    std::string directory;
    if (const char* value=SDL_getenv("CUBESHOT_DIRECTORY_URL")) directory=value;
    for (int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if (option=="--help") { std::cout << "cubeshot_client [--directory-url https://example.org]\n"; return 0; }
        if (option=="--directory-url" && i+1<argc) directory=argv[++i];
        else { std::cerr << "Unknown or incomplete argument\n"; return 1; }
    }
    cubeshot::client::Application application(directory);
    return application.run();
}
