// Uncensored Script: restores text that was changed or removed after Sands of Time's original version (1.0).
// Each entry replaces text of the current version (1.22) with the original's, once the message is loaded.

#include "sot.h"

// Sands of Time's Message_OpenText (uLib): loads the message into msgCtx->font.msgBuf
void func_80704978(PlayState* play, u16 textId);

// Message text: 0x01 new line, 0x04 new textbox, 0x02 end of the message
typedef struct {
    u16 textId;
    const char* current;  // in Sands of Time 1.22
    const char* original; // in Sands of Time 1.0
} ScriptChange;

static const ScriptChange sChanges[] = {
    // Great Fairy of Wisdom, about Big Brother's Trial
    { 0x00D8,
      "What? Big Brother's Trial? \x01No, this has nothing to do with that\x01"
      "braindead moron and his IQ of 64!",
      "Big Brother's Trial? \x01No, this has nothing to do with that \x01"
      "retard with IQs of 64!" },
    // Kokiri: the removed second textbox
    { 0x10B9,
      "Living here,\x01we'll never grow old.\x02",
      "Living here,\x01we'll never grow old.\x04Isn't that attractive for everyone?\x01Heh heh heh...\x02" },
};

static s32 Script_Length(const char* text) {
    s32 n = 0;

    while (text[n] != '\0') {
        n++;
    }
    return n;
}

static s32 Script_Find(const char* buf, s32 length, const char* text, s32 textLength) {
    s32 i;
    s32 j;

    for (i = 0; i + textLength <= length; i++) {
        for (j = 0; j < textLength; j++) {
            if (buf[i + j] != text[j]) {
                break;
            }
        }
        if (j == textLength) {
            return i;
        }
    }
    return -1;
}

// @mod Message_OpenText: restores the original text once the message is loaded.
RECOMP_PATCH void Message_OpenText(PlayState* play, u16 textId) {
    MessageContext* msgCtx = &play->msgCtx;
    char* buf = msgCtx->font.msgBuf;
    u32 c;

    func_80704978(play, textId);

    for (c = 0; c < ARRAY_COUNT(sChanges); c++) {
        const ScriptChange* change = &sChanges[c];
        s32 curLength;
        s32 origLength;
        s32 diff;
        s32 pos;
        s32 i;

        if (change->textId != msgCtx->textId) {
            continue;
        }

        curLength = Script_Length(change->current);
        origLength = Script_Length(change->original);
        diff = origLength - curLength;
        pos = Script_Find(buf, msgCtx->msgLength, change->current, curLength);
        if ((pos < 0) || (msgCtx->msgLength + diff > (s32)sizeof(msgCtx->font.msgBuf))) {
            continue;
        }

        // Move the rest of the message, then write the original text
        if (diff > 0) {
            for (i = msgCtx->msgLength - 1; i >= pos + curLength; i--) {
                buf[i + diff] = buf[i];
            }
        } else if (diff < 0) {
            for (i = pos + curLength; i < msgCtx->msgLength; i++) {
                buf[i + diff] = buf[i];
            }
        }
        for (i = 0; i < origLength; i++) {
            buf[pos + i] = change->original[i];
        }
        msgCtx->msgLength += diff;
    }
}
