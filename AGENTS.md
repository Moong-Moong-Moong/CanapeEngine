# Line endings

This is a Windows Visual Studio repository. Keep text files in CRLF format.
When editing an existing file, preserve its encoding and use CRLF for every line,
including newly added lines. After an edit, verify that the file has no mixed
line endings (for tracked files, `git ls-files --eol` must not show `w/mixed`).
