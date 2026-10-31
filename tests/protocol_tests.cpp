#include "network/Address.h"
#include "network/Protocol.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::network;

void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        Match m;
        m.connect(1);
        m.connect(2);
        m.setReady(1, true);
        m.setReady(2, true);
        const auto original = m.snapshot();
        auto e = envelope();
        *e.mutable_snapshot() = encodeSnapshot(original);
        const auto bytes = e.SerializeAsString();
        wire::Envelope decoded;
        expect(decode(bytes.data(), bytes.size(), decoded), "Decode valid envelope");
        auto s = decodeSnapshot(decoded.snapshot());
        expect(s && s->phase == original.phase && s->round == original.round && s->players[1].id == 2 &&
                   s->ready[1],
               "Snapshot roundtrip");
        auto invalid = decoded.snapshot();
        invalid.mutable_players(0)->set_health(101);
        expect(!decodeSnapshot(invalid), "Reject invalid health");
        invalid = decoded.snapshot();
        invalid.mutable_players(0)->mutable_position()->set_x(std::numeric_limits<float>::quiet_NaN());
        expect(!decodeSnapshot(invalid), "Reject NaN positions");
        invalid = decoded.snapshot();
        invalid.mutable_players(1)->set_id(1);
        expect(!decodeSnapshot(invalid), "Reject duplicate player IDs");
        invalid = decoded.snapshot();
        invalid.set_phase(999);
        expect(!decodeSnapshot(invalid), "Reject unknown phase");
        auto rich = original;
        rich.phase = MatchPhase::Armory;
        rich.players[0].weapon.equip(WeaponKind::Sniper);
        rich.players[0].weapon.charging = true;
        rich.players[0].weapon.preparation = .2f;
        rich.weaponChoice = {WeaponChoice::Spinning, WeaponChoice::Ready};
        rich.rolled[0] = WeaponKind::Shotgun;
        rich.spinRemaining[0] = 2;
        Corpse corpse;
        corpse.body = {10, 2, {300, 420}, {30, -20}, {48, 32}, .4f, 2};
        corpse.integrity = 80;
        corpse.body.outline = {{-24, -24}, {24, -24}, {24, 24}, {-24, 24}};
        refreshPiece(corpse.body);
        rich.corpses.push_back(corpse);
        rich.pieces.push_back({11, 2, {310, 400}, {80, -60}, {11, 7}, .3f, 4});
        rich.pieces.back().outline = {{-5.5f, -3.5f}, {5.5f, -3.5f}, {5.5f, 3.5f}, {-5.5f, 3.5f}};
        refreshPiece(rich.pieces.back());
        auto encodedRich = encodeSnapshot(rich);
        auto decodedRich = decodeSnapshot(encodedRich);
        expect(decodedRich && decodedRich->corpses[0].body.outline.size() == 4 &&
                   decodedRich->pieces[0].id == 11 && decodedRich->players[0].weapon.charging &&
                   decodedRich->players[0].weapon.kind == WeaponKind::Sniper &&
                   decodedRich->rolled[0] == WeaponKind::Shotgun,
               "Weapon, roulette and physical remains roundtrip");
        auto badPiece = encodedRich;
        badPiece.mutable_pieces(0)->set_id(10);
        expect(!decodeSnapshot(badPiece), "Reject duplicate remains identities");
        badPiece = encodedRich;
        badPiece.mutable_corpses(0)->mutable_body()->mutable_outline(0)->set_x(1000);
        expect(!decodeSnapshot(badPiece), "Reject invalid corpse contour");
        badPiece = encodedRich;
        auto* crossed = badPiece.mutable_pieces(0);
        crossed->clear_outline();
        for (auto at : {Vec2{-10, -10}, Vec2{10, 10}, Vec2{-10, 10}, Vec2{10, -10}})
        {
            auto* v = crossed->add_outline();
            v->set_x(at.x);
            v->set_y(at.y);
        }
        expect(!decodeSnapshot(badPiece), "Reject self-intersecting physical outlines from the network");
        badPiece = encodedRich;
        auto* surface = badPiece.mutable_players(0)->mutable_surface();
        for (int n = 0; n < 2; ++n)
        {
            auto* c = surface->add_chips();
            c->set_edge(0);
            c->set_along(24);
            c->set_width(12);
            c->set_depth(6);
        }
        expect(!decodeSnapshot(badPiece), "Reject overlapping damage chips from the network");
        badPiece = encodedRich;
        badPiece.mutable_pieces(0)->set_angle(std::numeric_limits<float>::quiet_NaN());
        expect(!decodeSnapshot(badPiece), "Reject non-finite physical remains");
        badPiece = encodedRich;
        badPiece.set_weapon_choice(0, 3);
        expect(!decodeSnapshot(badPiece), "Reject invalid roulette states");
        badPiece = encodedRich;
        badPiece.mutable_players(0)->set_weapon(99);
        expect(!decodeSnapshot(badPiece), "Reject unknown weapon kind");
        auto input = envelope();
        auto* command = input.mutable_control()->add_commands();
        command->set_sequence(1);
        command->set_fire_held(true);
        command->mutable_aim()->set_x(1);
        auto inputBytes = input.SerializeAsString();
        wire::Envelope inputDecoded;
        expect(decode(inputBytes.data(), inputBytes.size(), inputDecoded) &&
                   inputDecoded.control().commands(0).fire_held(),
               "Continuous trigger state survives network serialization");
        e.set_protocol(999);
        auto wrong = e.SerializeAsString();
        expect(!decode(wrong.data(), wrong.size(), decoded), "Reject incompatible version");
        expect(!decode(bytes.data(), maxPacketBytes + 1, decoded), "Reject large packet before reading it");
        expect(!decode("bad", 3, decoded), "Reject malformed packet");
        for (const char* address : {"127.0.0.1", "192.168.1.10:27015", "10.0.0.1:1", "255.255.255.255:65535"})
            expect(validAddress(address), "Accept IPv4 and optional port");
        for (const char* address : {"", "localhost", "1.2.3", "1.2.3.4.5", "1.2.3.256", "1.2.3.4:0",
                                    "1.2.3.4:65536", "1.2.3.4:12abc", "1.2.3.4:"})
            expect(!validAddress(address), "Reject invalid IPv4 or port");
        std::cout << "Protocol checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
