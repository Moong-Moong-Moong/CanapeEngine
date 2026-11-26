#include "Undo/UndoHistory.h"

#include <cstdio>
#include <string>

namespace
{
    int g_Failures = 0;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::printf("FAIL: %s\n", message);
            ++g_Failures;
        }
    }

    // Scene text is just the step number here: step n goes from "n-1" to "n".
    UndoHistory::Entry Step(int n)
    {
        return { "step " + std::to_string(n), std::to_string(n - 1), std::to_string(n), static_cast<uint64_t>(n - 1),
            static_cast<uint64_t>(n) };
    }

    void BasicUndoRedo()
    {
        UndoHistory history;
        Require(!history.CanUndo() && !history.CanRedo(), "empty history has nothing to undo or redo");
        Require(history.Undo() == nullptr && history.Redo() == nullptr, "empty undo/redo return null");

        history.Push(Step(1));
        history.Push(Step(2));
        Require(history.GetUndoCount() == 2 && history.GetRedoCount() == 0, "two steps recorded");
        Require(history.PeekUndo() && history.PeekUndo()->label == "step 2", "undo label is the latest step");

        const UndoHistory::Entry* undone = history.Undo();
        Require(undone && undone->before == "1" && undone->selectionBefore == 1, "undo returns the scene before step 2");
        Require(history.CanRedo() && history.PeekRedo()->label == "step 2", "step 2 can be redone");

        const UndoHistory::Entry* redone = history.Redo();
        Require(redone && redone->after == "2" && redone->selectionAfter == 2, "redo returns the scene after step 2");
        Require(!history.CanRedo(), "nothing left to redo");
    }

    void NewEditDropsRedo()
    {
        UndoHistory history;
        history.Push(Step(1));
        history.Push(Step(2));
        history.Push(Step(3));
        history.Undo();
        history.Undo();
        Require(history.GetRedoCount() == 2, "two steps undone");
        history.Push(Step(10));
        Require(history.GetRedoCount() == 0, "a new edit discards the redo steps");
        Require(history.GetUndoCount() == 2 && history.PeekUndo()->label == "step 10", "new edit follows step 1");
    }

    void LimitDropsOldest()
    {
        UndoHistory history;
        Require(history.GetLimit() == 5, "default limit is 5");
        for (int n = 1; n <= 8; ++n)
        {
            history.Push(Step(n));
        }
        Require(history.GetUndoCount() == 5, "only 5 steps kept");
        int undone = 0;
        const UndoHistory::Entry* last = nullptr;
        while (const UndoHistory::Entry* entry = history.Undo())
        {
            last = entry;
            ++undone;
        }
        Require(undone == 5 && last && last->label == "step 4", "oldest kept step is step 4");
    }

    void ShrinkingLimit()
    {
        UndoHistory history;
        history.SetLimit(10);
        for (int n = 1; n <= 6; ++n)
        {
            history.Push(Step(n));
        }
        history.Undo();
        history.Undo();
        Require(history.GetUndoCount() == 4 && history.GetRedoCount() == 2, "4 undo / 2 redo before shrinking");
        history.SetLimit(3);
        Require(history.GetUndoCount() == 3, "shrinking keeps the newest undo steps");
        Require(history.GetRedoCount() == 2, "redo steps within the limit stay");
        Require(history.PeekUndo()->label == "step 4" && history.PeekRedo()->label == "step 5", "cursor still between 4 and 5");

        history.SetLimit(1);
        Require(history.GetUndoCount() == 1 && history.GetRedoCount() == 1, "limit 1 keeps one each way");
        history.SetLimit(0);
        Require(history.GetLimit() == 1, "limit never drops below 1");
    }

    void ClearAndMemory()
    {
        UndoHistory history;
        history.Push({ "a", "1234", "567", 0, 0 });
        Require(history.GetMemoryBytes() == 1 + 4 + 3, "memory counts label and both snapshots");
        history.Clear();
        Require(!history.CanUndo() && !history.CanRedo() && history.GetMemoryBytes() == 0, "clear empties the history");
    }
}

int main()
{
    BasicUndoRedo();
    NewEditDropsRedo();
    LimitDropsOldest();
    ShrinkingLimit();
    ClearAndMemory();

    if (g_Failures == 0)
    {
        std::printf("UndoHistory validation passed\n");
        return 0;
    }
    std::printf("UndoHistory validation: %d failure(s)\n", g_Failures);
    return 1;
}
