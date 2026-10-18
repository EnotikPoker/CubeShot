#pragma once

namespace cubeshot::network
{
// Reproducible transport tests only; zero disables simulation.
void simulateNetwork(float lossPercent, int lagMilliseconds, int jitterMilliseconds = 0);
} // namespace cubeshot::network
