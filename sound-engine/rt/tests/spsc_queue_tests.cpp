#include <pianolab/rt/spsc_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>

using pianolab::rt::SpscQueue;

TEST_CASE("a queue hands items over in order") {
    SpscQueue<int> queue(4);
    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));
    CHECK(queue.tryPush(3));
    CHECK(queue.tryPop() == 1);
    CHECK(queue.tryPop() == 2);
    CHECK(queue.tryPop() == 3);
}

TEST_CASE("popping an empty queue gives nothing") {
    SpscQueue<int> queue(2);
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.tryPush(7));
    CHECK(queue.tryPop() == 7);
    CHECK_FALSE(queue.tryPop().has_value());
}

TEST_CASE("pushing to a full queue fails and keeps what it holds") {
    SpscQueue<int> queue(3);
    const std::size_t capacity = queue.capacity();
    REQUIRE(capacity >= 3);
    for (std::size_t item = 0; item < capacity; ++item) {
        CHECK(queue.tryPush(static_cast<int>(item)));
    }
    CHECK_FALSE(queue.tryPush(99));
    for (std::size_t item = 0; item < capacity; ++item) {
        CHECK(queue.tryPop() == static_cast<int>(item));
    }
    CHECK_FALSE(queue.tryPop().has_value());
}

TEST_CASE("a queue keeps working as it wraps around many times") {
    SpscQueue<int> queue(3);
    for (int item = 0; item < 1000; ++item) {
        REQUIRE(queue.tryPush(item));
        REQUIRE(queue.tryPop() == item);
    }
}

TEST_CASE("a zero capacity still holds one item") {
    SpscQueue<int> queue(0);
    CHECK(queue.capacity() >= 1);
    CHECK(queue.tryPush(5));
    CHECK(queue.tryPop() == 5);
}

TEST_CASE("two threads hammering a queue lose, duplicate and reorder nothing") {
    // A small queue, so the producer keeps finding it full and the consumer keeps finding it
    // empty: both edges get exercised thousands of times.
    constexpr std::uint64_t count = 2'000'000;
    SpscQueue<std::uint64_t> queue(64);

    std::uint64_t received = 0;
    std::uint64_t firstWrong = 0;
    bool inOrder = true;
    std::thread consumer([&] {
        std::uint64_t expected = 0;
        while (expected < count) {
            if (const auto item = queue.tryPop()) {
                if (*item != expected && inOrder) {
                    inOrder = false;
                    firstWrong = *item;
                }
                ++expected;
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (std::uint64_t item = 0; item < count; ++item) {
        while (!queue.tryPush(item)) {
            std::this_thread::yield();
        }
    }
    consumer.join();

    CHECK(received == count);
    INFO("first out-of-order item: " << firstWrong);
    CHECK(inOrder);
    CHECK_FALSE(queue.tryPop().has_value());
}
