#include "game/Prediction.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Match match; match.connect(1); match.connect(2); match.setReady(1,true); match.setReady(2,true);
        for (int i=0;i<361;++i) match.update(Prediction::step);
        auto snapshot = match.snapshot(); Prediction prediction; prediction.reconcile(snapshot,1);
        const auto initial = prediction.player()->position;
        for (int i=0;i<12;++i) prediction.advance({1,false,false,{1,0}});
        expect(prediction.player()->position.x > initial.x + 20, "Movement is visible before any server reply");
        expect(prediction.player()->health == 100, "Prediction never assigns damage");
        // Snapshot accounts for the first six commands; replay must retain the other six exactly once.
        for (int i=0;i<6;++i) { match.setInput(1,{1,false,false,{1,0}}); match.update(Prediction::step); }
        snapshot = match.snapshot(); snapshot.inputAck[0] = 6; prediction.reconcile(snapshot,1);
        expect(std::abs(prediction.player()->position.x - initial.x - Player::moveSpeed * 12 / 120) < .02f, "Reconcile replays only unacknowledged commands");
        expect(prediction.commands().size() == 6, "Acknowledged history discarded");
        prediction.action(1,true,false,{1,0}); prediction.advance({1,false,false,{1,0}});
        expect(prediction.player()->velocity.y < 0, "Jump is predicted with common physics");
        prediction.action(2,false,true,{1,0});
        expect(prediction.bullets().size() == 1 && prediction.bullets()[0].shotSequence == 2, "Shot is visible immediately and has a stable action key");
        snapshot.actionAck[0] = 2; prediction.reconcile(snapshot,1);
        expect(prediction.bullets().empty(), "Acknowledgement removes preliminary bullet even for rejected or already collided shot");
        for (int i=0;i<500;++i) prediction.advance({1,false,false,{1,0}});
        expect(prediction.commands().size() <= 240, "Input history bounded to two seconds");
        snapshot.round++; snapshot.phase = MatchPhase::Countdown; prediction.reconcile(snapshot,1);
        expect(prediction.commands().empty() && prediction.bullets().empty(), "Round transition clears pending effects");
        snapshot.phase=MatchPhase::Round; snapshot.players[0].health=0; prediction.reconcile(snapshot,1);
        prediction.advance({1,false,false,{1,0}}); prediction.action(3,false,true,{1,0});
        expect(prediction.commands().empty() && prediction.bullets().empty(), "Dead player cannot predict actions");
        prediction.reset(); expect(!prediction.player(), "Disconnect clears prediction");
        std::cout << "Prediction reconciliation checks passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
