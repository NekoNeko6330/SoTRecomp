// Wisdom Fairy 1.0 Text: the Great Fairy of Wisdom's line about Big Brother's Trial, as it was in Sands of Time 1.0.

#include "sot.h"

// Sands of Time's Message_OpenText (uLib): loads the message into msgCtx->font.msgBuf
void func_80704978(PlayState* play, u16 textId);

// The textbox in 1.22 (0x01: new line)
static const char sTextbox122[] = "What? Big Brother's Trial? \x01No, this has nothing to do with that\x01"
                                  "braindead moron and his IQ of 64!";
// The textbox in 1.0
static const char sTextbox10[] = "Big Brother's Trial? \x01No, this has nothing to do with that \x01"
                                 "retard with IQs of 64!";

static s32 Text_Find(const char* buf, s32 length, const char* text, s32 textLength) {
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

// @mod Message_OpenText: replaces the textbox once the message is loaded.
RECOMP_PATCH void Message_OpenText(PlayState* play, u16 textId) {
    MessageContext* msgCtx = &play->msgCtx;
    char* buf = msgCtx->font.msgBuf;
    s32 newLength = sizeof(sTextbox122) - 1;
    s32 oldLength = sizeof(sTextbox10) - 1;
    s32 pos;
    s32 i;

    func_80704978(play, textId);

    pos = Text_Find(buf, msgCtx->msgLength, sTextbox122, newLength);
    if (pos < 0) {
        return;
    }

    // The 1.0 textbox is shorter: move the rest of the message back
    for (i = 0; i < oldLength; i++) {
        buf[pos + i] = sTextbox10[i];
    }
    for (i = pos + newLength; i < msgCtx->msgLength; i++) {
        buf[i - (newLength - oldLength)] = buf[i];
    }
    msgCtx->msgLength -= newLength - oldLength;
}
