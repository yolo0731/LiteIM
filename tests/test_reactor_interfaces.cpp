#include "TestUtil.hpp"

#include "net/Channel.hpp"
#include "net/Epoller.hpp"
#include "net/EventLoop.hpp"

#include <cstdint>
#include <sys/epoll.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using liteim::net::Channel;
using liteim::net::Epoller;
using liteim::net::EventLoop;

static_assert(std::is_class_v<Epoller>);
static_assert(std::is_class_v<Channel>);
static_assert(std::is_class_v<EventLoop>);

static_assert(!std::is_copy_constructible_v<Epoller>);
static_assert(!std::is_copy_assignable_v<Epoller>);
static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_copy_assignable_v<Channel>);
static_assert(!std::is_copy_constructible_v<EventLoop>);
static_assert(!std::is_copy_assignable_v<EventLoop>);

static_assert(std::is_constructible_v<Channel, EventLoop*, int>);
static_assert(std::is_same_v<decltype(std::declval<const Channel&>().fd()), int>);
static_assert(std::is_same_v<decltype(std::declval<const Channel&>().events()), std::uint32_t>);
static_assert(std::is_same_v<decltype(std::declval<const Channel&>().revents()), std::uint32_t>);
static_assert(std::is_same_v<decltype(std::declval<Channel&>().setRevents(0)), void>);

static_assert(std::is_same_v<decltype(std::declval<Epoller&>().poll(0)), std::vector<Epoller::ActiveEvent>>);
static_assert(std::is_same_v<decltype(std::declval<EventLoop&>().loop()), void>);
static_assert(std::is_same_v<decltype(std::declval<EventLoop&>().quit()), void>);
static_assert(std::is_same_v<decltype(std::declval<EventLoop&>().updateChannel(nullptr)), void>);
static_assert(std::is_same_v<decltype(std::declval<EventLoop&>().removeChannel(nullptr)), void>);

void testChannelEventConstants() {
    liteim::tests::expect(Channel::kNoneEvent == 0, "none event should be zero");
    liteim::tests::expect((Channel::kReadEvent & EPOLLIN) != 0, "read event should include EPOLLIN");
    liteim::tests::expect((Channel::kReadEvent & EPOLLPRI) != 0, "read event should include EPOLLPRI");
    liteim::tests::expect(Channel::kWriteEvent == EPOLLOUT, "write event should be EPOLLOUT");
}

void testEpollerActiveEventIsPlainState() {
    Epoller::ActiveEvent active_event;
    active_event.channel = nullptr;
    active_event.events = EPOLLIN | EPOLLOUT;

    liteim::tests::expect(active_event.channel == nullptr, "active event should store channel pointer");
    liteim::tests::expect((active_event.events & EPOLLIN) != 0, "active event should store readable flag");
    liteim::tests::expect((active_event.events & EPOLLOUT) != 0, "active event should store writable flag");
}

void testChannelCallbackTypeCanStoreCallable() {
    bool called = false;
    Channel::EventCallback callback = [&called]() {
        called = true;
    };

    callback();

    liteim::tests::expect(called, "channel callback type should store void callable");
}

}  // namespace

std::vector<liteim::tests::TestCase> reactorInterfaceTests() {
    return {
        {"reactor interface channel event constants", testChannelEventConstants},
        {"reactor interface active event state", testEpollerActiveEventIsPlainState},
        {"reactor interface callback type", testChannelCallbackTypeCanStoreCallable},
    };
}
