#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

// Linear undo / redo stack of whole-scene snapshots. No engine dependencies so it can be validated on its own.
class UndoHistory
{
public:
    struct Entry
    {
        std::string label;
        std::string before; // scene before the edit (SceneSerializer::SaveToString)
        std::string after;  // scene after the edit
        uint64_t selectionBefore = 0;
        uint64_t selectionAfter = 0;
    };

    static constexpr size_t kDefaultLimit = 5;

    // Number of undo steps kept. Shrinking drops the oldest steps (and redo steps beyond the new size).
    void SetLimit(size_t limit);
    size_t GetLimit() const { return m_Limit; }

    // Records a new edit. Anything that could be redone is discarded, and the oldest step goes when over the limit.
    void Push(Entry entry);

    bool CanUndo() const { return m_Cursor > 0; }
    bool CanRedo() const { return m_Cursor < m_Entries.size(); }
    const Entry* PeekUndo() const { return CanUndo() ? &m_Entries[m_Cursor - 1] : nullptr; }
    const Entry* PeekRedo() const { return CanRedo() ? &m_Entries[m_Cursor] : nullptr; }

    // Move the cursor and return the entry to apply (its `before` for undo, `after` for redo).
    const Entry* Undo();
    const Entry* Redo();

    void Clear();
    size_t GetUndoCount() const { return m_Cursor; }
    size_t GetRedoCount() const { return m_Entries.size() - m_Cursor; }
    size_t GetMemoryBytes() const;

private:
    void Trim();

    std::deque<Entry> m_Entries; // [0, cursor) can be undone, [cursor, size) can be redone
    size_t m_Cursor = 0;
    size_t m_Limit = kDefaultLimit;
};
