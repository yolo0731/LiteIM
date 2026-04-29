#include "TestUtil.hpp"

#include <vector>

std::vector<liteim::tests::TestCase> protocolTests();
std::vector<liteim::tests::TestCase> frameDecoderTests();
std::vector<liteim::tests::TestCase> bufferTests();

int main() {
    std::vector<liteim::tests::TestCase> tests;

    const auto protocol_tests = protocolTests();
    tests.insert(tests.end(), protocol_tests.begin(), protocol_tests.end());

    const auto frame_decoder_tests = frameDecoderTests();
    tests.insert(tests.end(), frame_decoder_tests.begin(), frame_decoder_tests.end());

    const auto buffer_tests = bufferTests();
    tests.insert(tests.end(), buffer_tests.begin(), buffer_tests.end());

    return liteim::tests::runTests(tests);
}
