#pragma once

#define CHANGELOG_NOTE_MAX 8

typedef struct {
    const char *version;
    const char *date;
    const char *summary;
    const char *notes[CHANGELOG_NOTE_MAX];
    int note_count;
} changelog_entry_t;

int changelog_count(void);
const changelog_entry_t *changelog_entry_at(int index);
const changelog_entry_t *changelog_latest(void);
