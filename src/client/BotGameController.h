#pragma once
#include "client/Menu.h"
#include "game/BotMatch.h"
#include <memory>

namespace cubeshot::client
{
// Owns the autonomous match lifetime; Application coordinates input and presentation.
class BotGameController
{
public:
    void handle(MenuAction action, Menu& menu, const std::string& nickname);
    void synchronize(Menu& menu);
    void control(const PlayerInput& input);
    void action(bool jump, bool shoot, Vec2 aim);
    void clearInput();
    void update(float dt, const Menu& menu);
    BotMatch* match();
    const BotMatch* match() const;

private:
    std::unique_ptr<BotMatch> match_;
};
} // namespace cubeshot::client
