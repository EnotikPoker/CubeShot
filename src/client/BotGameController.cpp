#include "client/BotGameController.h"
#include <random>

namespace cubeshot::client
{
void BotGameController::handle(MenuAction action, Menu& menu, const std::string& nickname)
{
    if (action == MenuAction::StartBots)
        match_ =
            std::make_unique<BotMatch>(menu.botMap(), menu.botDifficulty(), nickname, std::random_device{}());
    if (action == MenuAction::BotRematch && match_)
        match_->rematch(nickname);
    if (match_ && (action == MenuAction::RollWeapon || action == MenuAction::KeepWeapon))
        match_->chooseWeapon(action == MenuAction::RollWeapon);
    synchronize(menu);
}

void BotGameController::synchronize(Menu& menu)
{
    if (!menu.bots())
        match_.reset();
    if (match_)
        menu.botState(match_->snapshot());
}

void BotGameController::control(const PlayerInput& input)
{
    if (match_)
        match_->control(input);
}

void BotGameController::action(bool jump, bool shoot, Vec2 aim)
{
    if (match_)
        match_->action(jump, shoot, aim);
}

void BotGameController::clearInput()
{
    if (match_)
        match_->clearInput();
}

void BotGameController::update(float dt, const Menu& menu)
{
    if (match_ && menu.bots() && (menu.playing() || menu.screen() == Screen::Armory))
        match_->update(dt);
}

BotMatch* BotGameController::match()
{
    return match_.get();
}

const BotMatch* BotGameController::match() const
{
    return match_.get();
}
} // namespace cubeshot::client
