#include "game/AttackQueries.h"
#include "game/BodyPhysics.h"
#include "game/BotController.h"
#include "game/BotQueries.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cubeshot
{
using namespace botdetail;

void BotController::cancelRoute(bool failed)
{
    if (failed && pathIndex_ < path_.size())
    {
        avoidFrom_ = static_cast<int>(path_[pathIndex_].from);
        avoidTo_ = static_cast<int>(path_[pathIndex_].to);
        avoidedUntil_ = time_ + 2;
    }
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = -1;
    leftGround_ = false;
    stuck_ = 0;
    replan_ = 0;
}

PlayerInput BotController::navigate(const Player& self, Vec2 goal, float dt)
{
    PlayerInput input;
    const auto& nodes = navigation_->nodes();
    if (nodes.empty())
        return input;
    if (distance(self.position, progress_) >= 8)
    {
        progress_ = self.position;
        stuck_ = 0;
    }
    else if (pathIndex_ < path_.size())
        stuck_ += dt;
    if (stuck_ >= 1)
        cancelRoute(true);
    if (time_ >= replan_ && self.grounded && traversalTick_ < 0)
    {
        path_ = navigation_->route(self.position, goal, time_ < avoidedUntil_ ? avoidFrom_ : -1,
                                   time_ < avoidedUntil_ ? avoidTo_ : -1, routeHazards_);
        pathIndex_ = 0;
        replan_ = time_ + .6f;
    }
    if (pathIndex_ >= path_.size())
    {
        path_.clear();
        pathIndex_ = 0;
        if (self.grounded && std::abs(goal.y - self.position.y) < 2 &&
            navigation_->nearest(self.position) == navigation_->nearest(goal))
            input.moveDirection =
                std::clamp((goal.x - self.position.x) / (self.movementSpeed() * step), -1.f, 1.f);
        else if (!self.grounded)
            input.moveDirection = self.velocity.x / self.movementSpeed();
        return input;
    }
    const auto& edge = path_[pathIndex_];
    const Vec2 start = nodes[edge.from].position, end = nodes[edge.to].position;
    if (!edge.jump && nodes[edge.from].surface == nodes[edge.to].surface)
    {
        if (!self.grounded || std::abs(self.position.y - end.y) > 2)
        {
            cancelRoute(true);
            input.moveDirection = goal.x > self.position.x ? 1.f : -1.f;
            return input;
        }
        const float dx = end.x - self.position.x;
        if (std::abs(dx) <= .05f)
            ++pathIndex_;
        else
            input.moveDirection = std::clamp(dx / (self.movementSpeed() * step), -1.f, 1.f);
        return input;
    }
    if (traversalTick_ < 0)
    {
        const float dx = start.x - self.position.x;
        if (std::abs(dx) > .05f || std::abs(start.y - self.position.y) > 2 || !self.grounded)
        {
            input.moveDirection = std::clamp(dx / (self.movementSpeed() * step), -1.f, 1.f);
            return input;
        }
        traversalTick_ = 1;
        leftGround_ = false;
        input.jump = edge.jump;
        input.jetpackHeld = edge.jetpack;
        input.moveDirection = static_cast<float>(edge.direction);
        return input;
    }
    leftGround_ = leftGround_ || !self.grounded;
    if (self.grounded && (leftGround_ || (edge.jump && traversalTick_ >= 4)))
    {
        if (std::abs(self.position.y - end.y) > 2 || std::abs(self.position.x - end.x) > 35)
            cancelRoute(true);
        else
        {
            traversalTick_ = -1;
            leftGround_ = false;
            ++pathIndex_;
        }
        return input;
    }
    if (traversalTick_ > edge.durationTicks + 45)
    {
        cancelRoute(true);
        return input;
    }
    const float baselineSpeed = Player::moveSpeed * self.abilities.speed();
    const int hold = static_cast<int>(std::lround(edge.holdTicks * baselineSpeed / self.movementSpeed()));
    input.moveDirection = traversalTick_ < hold ? static_cast<float>(edge.direction) : 0.f;
    input.jump = traversalTick_ == edge.secondJumpTick;
    input.jetpackHeld = edge.jetpack && traversalTick_ < edge.holdTicks;
    ++traversalTick_;
    return input;
}

} // namespace cubeshot
