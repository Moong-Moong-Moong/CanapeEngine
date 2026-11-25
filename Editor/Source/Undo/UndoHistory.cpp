#include "Undo/UndoHistory.h"

#include <algorithm>

void UndoHistory::SetLimit(size_t limit)
{
    m_Limit = std::max<size_t>(limit, 1);
    Trim();
}

void UndoHistory::Push(Entry entry)
{
    m_Entries.erase(m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Cursor), m_Entries.end());
    m_Entries.push_back(std::move(entry));
    m_Cursor = m_Entries.size();
    Trim();
}

const UndoHistory::Entry* UndoHistory::Undo()
{
    if (!CanUndo())
    {
        return nullptr;
    }
    --m_Cursor;
    return &m_Entries[m_Cursor];
}

const UndoHistory::Entry* UndoHistory::Redo()
{
    if (!CanRedo())
    {
        return nullptr;
    }
    return &m_Entries[m_Cursor++];
}

void UndoHistory::Clear()
{
    m_Entries.clear();
    m_Cursor = 0;
}

size_t UndoHistory::GetMemoryBytes() const
{
    size_t bytes = 0;
    for (const Entry& entry : m_Entries)
    {
        bytes += entry.label.size() + entry.before.size() + entry.after.size();
    }
    return bytes;
}

void UndoHistory::Trim()
{
    // Oldest undo steps go first.
    while (m_Cursor > m_Limit)
    {
        m_Entries.pop_front();
        --m_Cursor;
    }
    // Redo steps beyond the limit (only after the limit shrank).
    while (m_Entries.size() - m_Cursor > m_Limit)
    {
        m_Entries.pop_back();
    }
}
