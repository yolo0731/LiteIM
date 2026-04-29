#include "TestUtil.hpp"

#include "net/Buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using liteim::net::Buffer;
using liteim::tests::TestCase;
using liteim::tests::expect;

void testAppendAndReadableBytes() {
    Buffer buffer;
    const std::string data = "hello";

    buffer.append(data.data(), data.size());

    expect(buffer.readableBytes() == data.size(), "readable bytes should match appended size");
    expect(std::string(buffer.peek(), buffer.readableBytes()) == data, "peek should expose readable data");
}

void testAppendString() {
    Buffer buffer;

    buffer.appendString("hello");
    buffer.appendString(" world");

    expect(buffer.readableBytes() == 11, "appendString should increase readable bytes");
    expect(buffer.retrieveAllAsString() == "hello world", "appendString data mismatch");
}

void testRetrievePartial() {
    Buffer buffer;
    buffer.appendString("abcdef");

    buffer.retrieve(2);

    expect(buffer.readableBytes() == 4, "partial retrieve should consume two bytes");
    expect(std::string(buffer.peek(), buffer.readableBytes()) == "cdef", "remaining data mismatch");
}

void testRetrieveAllAsString() {
    Buffer buffer;
    buffer.appendString("abc");
    buffer.appendString("def");

    const std::string result = buffer.retrieveAllAsString();

    expect(result == "abcdef", "retrieveAllAsString should return all readable data");
    expect(buffer.readableBytes() == 0, "buffer should be empty after retrieveAllAsString");
}

void testRetrieveMoreThanReadableClearsBuffer() {
    Buffer buffer;
    buffer.appendString("abc");

    buffer.retrieve(100);

    expect(buffer.readableBytes() == 0, "retrieving more than readable bytes should clear buffer");
}

void testAppendAfterRetrieve() {
    Buffer buffer;
    buffer.appendString("abc");
    buffer.retrieve(2);
    buffer.appendString("def");

    expect(std::string(buffer.peek(), buffer.readableBytes()) == "cdef", "append after retrieve mismatch");
}

void testAppendNullWithNonZeroLengthThrows() {
    Buffer buffer;

    bool thrown = false;
    try {
        buffer.append(nullptr, 1);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "append nullptr with non-zero length should throw");
}

void testAppendNullWithZeroLengthIsNoop() {
    Buffer buffer;

    buffer.append(nullptr, 0);

    expect(buffer.readableBytes() == 0, "append nullptr with zero length should be no-op");
}

}  // namespace

std::vector<TestCase> bufferTests() {
    return {
        {"buffer append and readable bytes", testAppendAndReadableBytes},
        {"buffer appendString", testAppendString},
        {"buffer retrieve partial", testRetrievePartial},
        {"buffer retrieve all as string", testRetrieveAllAsString},
        {"buffer retrieve more than readable clears buffer", testRetrieveMoreThanReadableClearsBuffer},
        {"buffer append after retrieve", testAppendAfterRetrieve},
        {"buffer append null with non-zero length throws", testAppendNullWithNonZeroLengthThrows},
        {"buffer append null with zero length is noop", testAppendNullWithZeroLengthIsNoop},
    };
}

