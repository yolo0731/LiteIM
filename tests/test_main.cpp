#include "TestUtil.hpp"

#include <vector>

std::vector<liteim::tests::TestCase> protocolTests();
std::vector<liteim::tests::TestCase> frameDecoderTests();
std::vector<liteim::tests::TestCase> bufferTests();
std::vector<liteim::tests::TestCase> socketUtilTests();
std::vector<liteim::tests::TestCase> reactorInterfaceTests();
std::vector<liteim::tests::TestCase> epollerTests();
std::vector<liteim::tests::TestCase> eventLoopTests();
std::vector<liteim::tests::TestCase> channelTests();

int main() {
    std::vector<liteim::tests::TestCase> tests;

    const auto protocol_tests = protocolTests();
    tests.insert(tests.end(), protocol_tests.begin(), protocol_tests.end());

    const auto frame_decoder_tests = frameDecoderTests();
    tests.insert(tests.end(), frame_decoder_tests.begin(), frame_decoder_tests.end());

    const auto buffer_tests = bufferTests();
    tests.insert(tests.end(), buffer_tests.begin(), buffer_tests.end());

    const auto socket_util_tests = socketUtilTests();
    tests.insert(tests.end(), socket_util_tests.begin(), socket_util_tests.end());

    const auto reactor_interface_tests = reactorInterfaceTests();
    tests.insert(tests.end(), reactor_interface_tests.begin(), reactor_interface_tests.end());

    const auto epoller_tests = epollerTests();
    tests.insert(tests.end(), epoller_tests.begin(), epoller_tests.end());

    const auto event_loop_tests = eventLoopTests();
    tests.insert(tests.end(), event_loop_tests.begin(), event_loop_tests.end());

    const auto channel_tests = channelTests();
    tests.insert(tests.end(), channel_tests.begin(), channel_tests.end());

    return liteim::tests::runTests(tests);
}
