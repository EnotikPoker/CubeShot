#include "network/SnapshotInterpolation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace cubeshot;
using namespace cubeshot::network;
using namespace std::chrono_literals;

namespace
{
void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void interpolationBoundaries()
{
    const auto start = SnapshotInterpolation::Clock::time_point{};
    auto first = Match{}.snapshot();
    first.phase = MatchPhase::Round;
    first.round = 1;
    first.players[0].position = {100, 200};
    Bullet bullet;
    bullet.id = 7;
    bullet.position = {120, 180};
    first.bullets.push_back(bullet);
    auto latest = first;
    latest.players[0].position = {200, 300};
    latest.players[0].health = 80;
    latest.bullets[0].position = {220, 280};

    SnapshotInterpolation history;
    history.add(first, start);
    history.add(latest, start + 20ms);
    const auto sample = history.sample(latest, start + 43ms); // 33 ms render delay, midpoint.
    expect(std::abs(sample.players[0].position.x - 150) < .001f, "Interpolate player position by ID");
    expect(std::abs(sample.bullets[0].position.x - 170) < .001f, "Interpolate bullet position by ID");
    expect(sample.players[0].health == 80, "Health always comes from authoritative latest snapshot");

    auto boundary = latest;
    boundary.round = 2;
    expect(history.sample(boundary, start + 43ms).players[0].position.x == 200,
           "Never blend positions across rounds");
    boundary = latest;
    boundary.phase = MatchPhase::RoundEnd;
    expect(history.sample(boundary, start + 43ms).players[0].position.x == 200,
           "Never blend positions across match phases");
    boundary = latest;
    boundary.players[0].health = 0;
    expect(history.sample(boundary, start + 43ms).players[0].position.x == 200,
           "Dead players retain authoritative position");
    expect(history.sample(latest, start + 1s).players[0].position.x == 200,
           "Outside history use latest position without extrapolation");
    history.clear();
    expect(history.sample(latest, start + 43ms).players[0].position.x == 200,
           "Reconnect cannot reuse previous interpolation history");
}
} // namespace

int main()
{
    try
    {
        interpolationBoundaries();
        std::cout << "Snapshot interpolation boundaries passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
