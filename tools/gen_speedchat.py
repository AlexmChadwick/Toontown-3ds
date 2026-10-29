#!/usr/bin/env python3
"""Generate source/ui/speedchat_data.{h,cpp} from upstream Open Toontown.

    .venv/bin/python -I tools/gen_speedchat.py ref/open-toontown source/ui

Sources (read, never vendored):
  - toontown/chat/TTChatInputSpeedChat.py  `scStructure` (the stock toon menu tree),
    read with `ast` so no DirectGui/ShowBase import is needed.
  - otp/otpbase/OTPLocalizerEnglish.py     menu titles and SpeedChatStaticText phrases.
    TTLocalizerEnglish merges SpeedChatStaticTextToontown + SpeedChatStaticTextCommon;
    we do the same merge here.

Structure grammar (otp/speedchat/SCMenu.py appendFromStructure):
  int               static-text terminal (msgIndex sent with setSC)
  {int: emoteId}    static-text terminal with a linked emote
  ['TITLE', ...]    plain sub-menu
  [MenuClass, 'TITLE', ...]  dynamic sub-menu (toontasks, pet tricks, ...): skipped

The menus that TTChatInputSpeedChat.createSpeedChat() prepends (PROMOTIONAL, EMOTIONS,
MY PHRASES, UNITE!) and the optional PETS menu are not static text and are skipped.
Every SpeedChatStaticText entry is still emitted in the phrase table so remote setSC
messages from any menu (holiday, cog HQ, boarding, ...) decode.

Output is deterministic and has no heap use at runtime.
"""
import ast
import os
import sys

ROOT_TITLE = 'SpeedChat'
SKIPPED_PREFIX_MENUS = ['PROMOTIONAL (TTSCPromotionalMenu)',
                        'MY PHRASES (SCCustomMenu, setSCCustom)', 'UNITE! (TTSCResistanceMenu, setSCResistance)',
                        'PETS (scPetMenuStructure, only when base.wantPets)']


def load_localizer(repo):
    sys.dont_write_bytecode = True  # never write .pyc into the pinned ref/ submodule
    sys.path.insert(0, repo)
    import importlib
    mod = importlib.import_module('otp.otpbase.OTPLocalizerEnglish')
    phrases = dict(mod.SpeedChatStaticTextToontown)
    phrases.update(mod.SpeedChatStaticTextCommon)
    return mod, phrases


def load_structure(repo, loc):
    path = os.path.join(repo, 'toontown/chat/TTChatInputSpeedChat.py')
    with open(path, encoding='utf-8') as f:
        tree = ast.parse(f.read(), path)
    node = None
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'scStructure'
                                                for t in stmt.targets):
            node = stmt.value
    if node is None:
        sys.exit('scStructure not found')

    def conv(n):
        if isinstance(n, ast.Constant):
            return n.value
        if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) and n.value.id == 'OTPLocalizer':
            return getattr(loc, n.attr)
        if isinstance(n, ast.Name):
            return ('class', n.id)
        if isinstance(n, ast.List):
            return [conv(e) for e in n.elts]
        if isinstance(n, ast.Dict):
            return {conv(k): conv(v) for k, v in zip(n.keys, n.values)}
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -conv(n.operand)
        sys.exit(f'unsupported node {ast.dump(n)}')

    return conv(node)


def c_str(s):
    out = []
    for b in s.encode('utf-8'):
        c = chr(b)
        if c == '"' or c == '\\':
            out.append('\\' + c)
        elif 32 <= b < 127:
            out.append(c)
        elif b < 32 and b != 10:
            continue  # Panda TextProperties control codes (e.g. \x04 in 20269)
        else:
            out.append('\\%03o' % b)  # octal: never swallows following hex digits
    return '"' + ''.join(out) + '"'


def main(repo, outdir):
    loc, phrases = load_localizer(repo)
    structure = load_structure(repo, loc)

    menus = []  # [title, parent, items]; items: ('menu', idx) | ('term', msg, emote)
    skipped = []
    missing = []

    def build(title, parent, children):
        idx = len(menus)
        menus.append([title, parent, []])
        for child in children:
            emote = -1
            if isinstance(child, dict):
                (child, emote), = child.items()
            if isinstance(child, int):
                if child not in phrases:
                    missing.append(child)
                    continue
                menus[idx][2].append(('term', child, emote))
            elif isinstance(child, list):
                if isinstance(child[0], str):
                    sub = build(child[0], idx, child[1:])
                    menus[idx][2].append(('menu', sub))
                else:
                    skipped.append(f'{child[1]} ({child[0][1]}) under {title}')
            else:
                skipped.append(f'{child!r} under {title}')
        return idx

    build(ROOT_TITLE, -1, structure)
    # Public body emotes use setEmoteState, not phrase IDs or private setSCEmote.
    emotes = len(menus)
    menus.append([loc.SCMenuEmotions,0,[]])  # SCEmoteMenu's title, EMOTIONS
    for i,label in enumerate(loc.EmoteList):
        if i==16: continue  # legacy Laugh is doNothing; the dictionary's Laugh resolves to 24
        menus[emotes][2].append(('emote',i,label))
    menus[0][2].insert(0,('menu',emotes))

    items = []
    first = []
    for m in menus:
        first.append(len(items))
        items.extend(m[2])
    max_items = max(len(m[2]) for m in menus)
    depth = 0
    for i in range(len(menus)):
        d, p = 0, i
        while menus[p][1] >= 0:
            p = menus[p][1]
            d += 1
        depth = max(depth, d)
    assert len(menus) < 256 and max_items < 256 and len(items) < 65536
    terms = sum(1 for it in items if it[0] == 'term')
    sorted_ids = sorted(phrases)

    def glyphs(t):
        return sum(1 for ch in t if not ch.isspace())

    def label(it):
        return menus[it[1]][0] if it[0] == 'menu' else it[2] if it[0]=='emote' else phrases[it[1]]

    max_glyphs = max(glyphs(m[0]) + sum(glyphs(label(it)) for it in m[2]) for m in menus)
    phrase_bytes = sum(len(phrases[k].encode('utf-8')) + 1 for k in sorted_ids)

    h = f'''// GENERATED by tools/gen_speedchat.py from open-toontown. Do not edit.
// Stock SpeedChat tree (toontown/chat/TTChatInputSpeedChat.py scStructure) and the
// English SpeedChatStaticText phrase table (otp/otpbase/OTPLocalizerEnglish.py).
#pragma once
#include <cstdint>

namespace tt::sc {{

struct Menu {{
    const char* title;
    uint16_t firstItem; // index into kItems
    uint8_t numItems;
    int8_t parent; // -1 for the root
}};

struct Item {{
    const char* label; // phrase text for terminals, sub-menu title for menus
    uint16_t value;    // terminal: msgIndex (setSC uint16); menu: index into kMenus
    int8_t emote;      // terminal: linked emote id (OTP Emote index), -1 = none
    uint8_t isMenu;
}};

struct Phrase {{
    uint16_t msgIndex;
    const char* text;
}};

constexpr int kNumMenus = {len(menus)};
constexpr int kNumItems = {len(items)};
constexpr int kNumMenuTerminals = {terms};
constexpr int kMaxMenuItems = {max_items};
constexpr int kMaxDepth = {depth}; // root = depth 0
constexpr int kNumPhrases = {len(sorted_ids)};
constexpr int kMaxMenuGlyphs = {max_glyphs}; // title + all labels of the largest menu (C2D_TextBuf sizing)

extern const Menu kMenus[kNumMenus];  // kMenus[0] is the root
extern const Item kItems[kNumItems];
extern const Phrase kPhrases[kNumPhrases]; // sorted by msgIndex

// Decode a received setSC msgIndex (SCDecoders.decodeSCStaticTextMsg). nullptr if unknown.
const char* phraseText(uint16_t msgIndex);

}} // namespace tt::sc
'''

    cpp = ['// GENERATED by tools/gen_speedchat.py from open-toontown. Do not edit.',
           '#include "ui/speedchat_data.h"', '', 'namespace tt::sc {', '',
           'const Menu kMenus[kNumMenus] = {']
    for i, m in enumerate(menus):
        cpp.append(f'    {{{c_str(m[0])}, {first[i]}, {len(m[2])}, {m[1]}}}, // {i}')
    cpp += ['};', '', 'const Item kItems[kNumItems] = {']
    for it in items:
        if it[0] == 'menu':
            cpp.append(f'    {{{c_str(menus[it[1]][0])}, {it[1]}, -1, 1}},')
        elif it[0]=='emote':
            cpp.append(f'    {{{c_str(it[2])}, 65535, {it[1]}, 0}},')
        else:
            cpp.append(f'    {{{c_str(phrases[it[1]])}, {it[1]}, {it[2]}, 0}},')
    cpp += ['};', '', 'const Phrase kPhrases[kNumPhrases] = {']
    for k in sorted_ids:
        cpp.append(f'    {{{k}, {c_str(phrases[k])}}},')
    cpp += ['};', '',
            'const char* phraseText(uint16_t msgIndex) {',
            '    int lo = 0, hi = kNumPhrases - 1;',
            '    while (lo <= hi) {',
            '        const int mid = (lo + hi) / 2;',
            '        if (kPhrases[mid].msgIndex == msgIndex) return kPhrases[mid].text;',
            '        if (kPhrases[mid].msgIndex < msgIndex) lo = mid + 1;',
            '        else hi = mid - 1;',
            '    }',
            '    return nullptr;',
            '}', '', '} // namespace tt::sc', '']

    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, 'speedchat_data.h'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(h)
    with open(os.path.join(outdir, 'speedchat_data.cpp'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(cpp))

    print(f'speedchat: {len(menus)} menus (depth {depth}), {len(items)} items, {terms} terminals, '
          f'max {max_items} items/menu, max {max_glyphs} glyphs/menu')
    print(f'speedchat: {len(sorted_ids)} phrases in decode table, {phrase_bytes} bytes of text')
    for s in SKIPPED_PREFIX_MENUS:
        print(f'speedchat: skipped top-level {s}')
    for s in skipped:
        print(f'speedchat: skipped dynamic menu {s}')
    if missing:
        print(f'speedchat: WARNING {len(missing)} msgIndex without text: {missing}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
