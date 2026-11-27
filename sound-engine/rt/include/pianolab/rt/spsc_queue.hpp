#pragma once

#include <rigtorp/SPSCQueue.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

/// Real-time plumbing shared by the engine and the audio service: lock-free queues between threads.
/// Standard C++ only.
namespace pianolab::rt {

/// A fixed-capacity queue that hands items of type `T` from exactly one producer thread to
/// exactly one consumer thread, without locks and without waiting: a push to a full queue and a
/// pop from an empty one fail at once instead of blocking. The storage is allocated once, when
/// the queue is made, so pushing and popping never allocate. That makes it safe on the audio
/// thread.
///
/// Only one thread may ever push and only one (possibly another) may ever pop. Two producers
/// need two queues. The queue can't be copied or moved, because the threads hold on to it.
///
/// A class rather than a plain struct because it owns shared storage between threads. The
/// lock-free algorithm is rigtorp/SPSCQueue (https://github.com/rigtorp/SPSCQueue), verified
/// with GenMC; this wrapper keeps it behind our own small interface.
template <typename T>
    requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>
class SpscQueue {
public:
    /// A queue that holds at least `capacity` items (at least one). Setup code: allocates.
    explicit SpscQueue(std::size_t capacity) : queue(capacity) {}

    /// Producer only: adds `item` to the back of the queue. Returns false, leaving the queue
    /// unchanged, when it's full. Real-time safe when copying `T` is.
    [[nodiscard]] bool tryPush(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return queue.try_push(item);
    }

    /// Producer only: moves `item` to the back of the queue. Returns false, leaving the queue
    /// unchanged, when it's full. Real-time safe.
    [[nodiscard]] bool tryPush(T&& item) noexcept { return queue.try_push(std::move(item)); }

    /// Consumer only: removes and returns the item at the front, or nothing when the queue is
    /// empty. Real-time safe.
    [[nodiscard]] std::optional<T> tryPop() noexcept {
        std::optional<T> item;
        (void)queue.try_consume([&item](T& front) noexcept { item.emplace(std::move(front)); });
        return item;
    }

    /// How many items the queue can hold. Fixed after construction; any thread may ask.
    [[nodiscard]] std::size_t capacity() const noexcept { return queue.capacity(); }

private:
    rigtorp::SPSCQueue<T> queue;
};

} // namespace pianolab::rt
