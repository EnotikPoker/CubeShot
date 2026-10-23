#include "client/Application.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>

int main(int argc, char** argv)
{
    std::string directory;
    bool smoke = false;
    if (const char* value = SDL_getenv("CUBESHOT_DIRECTORY_URL"))
        directory = value;
    for (int i = 1; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--version")
        {
            std::cout << "CubeShot " CUBESHOT_VERSION "\n";
            return 0;
        }
        if (option == "--help")
        {
            std::cout << "CubeShot [--directory-url https://example.org] [--version] [--smoke-test]\n";
            return 0;
        }
        if (option == "--smoke-test")
            smoke = true;
        else if (option == "--directory-url" && i + 1 < argc)
            directory = argv[++i];
        else
        {
            std::cerr << "Unknown or incomplete argument\n";
            return 1;
        }
    }
    cubeshot::client::Application application(directory);
    const int result = smoke ? application.smokeTest() : application.run();
    if (result && !smoke)
    {
        const std::string message =
            std::string("Не удалось запустить или продолжить игру.\n") + SDL_GetError();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CubeShot", message.c_str(), nullptr);
    }
    return result;
}
