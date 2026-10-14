#include "network/Protocol.h"
#include "network/Session.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::network;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Match m; m.connect(1); m.connect(2); m.setReady(1, true); m.setReady(2, true);
        const auto original = m.snapshot();
        auto e = envelope(); *e.mutable_snapshot() = encodeSnapshot(original);
        const auto bytes = e.SerializeAsString(); wire::Envelope decoded;
        expect(decode(bytes.data(), bytes.size(), decoded), "Decode valid envelope");
        auto s = decodeSnapshot(decoded.snapshot());
        expect(s && s->phase == original.phase && s->round == original.round && s->players[1].id == 2 && s->ready[1], "Snapshot roundtrip");
        auto invalid = decoded.snapshot(); invalid.mutable_players(0)->set_health(101);
        expect(!decodeSnapshot(invalid), "Reject invalid health");
        invalid = decoded.snapshot(); invalid.mutable_players(0)->mutable_position()->set_x(std::numeric_limits<float>::quiet_NaN());
        expect(!decodeSnapshot(invalid), "Reject NaN positions");
        invalid = decoded.snapshot(); invalid.mutable_players(1)->set_id(1);
        expect(!decodeSnapshot(invalid), "Reject duplicate player IDs");
        invalid = decoded.snapshot(); invalid.set_phase(999);
        expect(!decodeSnapshot(invalid), "Reject unknown phase");
        e.set_protocol(999); auto wrong = e.SerializeAsString();
        expect(!decode(wrong.data(), wrong.size(), decoded), "Reject incompatible version");
        expect(!decode(bytes.data(), maxPacketBytes + 1, decoded), "Reject large packet before reading it");
        expect(!decode("bad", 3, decoded), "Reject malformed packet");
        for (const char* address : {"127.0.0.1", "192.168.1.10:27015", "10.0.0.1:1", "255.255.255.255:65535"})
            expect(validAddress(address), "Accept IPv4 and optional port");
        for (const char* address : {"", "localhost", "1.2.3", "1.2.3.4.5", "1.2.3.256", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:12abc", "1.2.3.4:"})
            expect(!validAddress(address), "Reject invalid IPv4 or port");
        std::cout << "Protocol checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
