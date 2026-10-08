#include "client/Application.h"

#include <SDL3/SDL_main.h>

int main(int, char**)
{
    cubeshot::client::Application application;
    return application.run();
}
