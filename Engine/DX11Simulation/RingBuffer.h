#pragma once

#include <array>
#include <cassert>
#include <utility>

namespace Canape
{
    // A queue of at most N over slots that live as long as the buffer. PushBack hands back the next slot as it
    // was last left, so whatever a slot owns is made once and reused.
    template<typename T, std::size_t N>
        requires (N > 0)
    class RingBuffer
    {
    private:
        std::array<T, N> slots;
        std::size_t front = 0;
        std::size_t count = 0;

    public:
        explicit RingBuffer(std::array<T, N> slots)
            : slots(std::move(slots)) {}

        T& Front()
        {
            assert(!Empty());
            return slots[front];
        }
        T& Back()
        {
            assert(!Empty());
            return slots[(front + count - 1) % N];
        }

        T& Push()
        {
            assert(!Full());
            ++count;
            return Back();
        }
        void Pop()
        {
            assert(!Empty());
            front = (front + 1) % N;
            --count;
        }

        bool Empty() const
        {
            return count == 0;
        }
        bool Full() const
        {
            return count == N;
        }
    };
}
