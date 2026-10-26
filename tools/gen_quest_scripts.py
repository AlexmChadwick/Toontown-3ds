#!/usr/bin/env python3
"""Generate source/quest/quest_scripts.{h,cpp} from the Toontorial scenes in QuestScripts.txt.

    .venv/bin/python -I tools/gen_quest_scripts.py            # writes both files
    .venv/bin/python -I tools/gen_quest_scripts.py --check    # exit 1 if they are stale
    .venv/bin/python -I tools/gen_quest_scripts.py --dump     # canonical text of every command

Sources (docs/design/tutorial.md section 3):
  ref/resources/phase_3/etc/QuestScripts.txt   the scripts, one "ID <name>" block each
  toontown/quest/QuestParser.py                getLineOfTokens: the upstream tokenizer, taken
                                               from the module with ast and run as is (the
                                               module itself needs Panda3D's otp libraries)
  toontown/toonbase/TTLocalizerEnglish.py      the chat strings (plain string assignments)

Only the scripts the Toontorial plays are generated (SCRIPTS below); upstream callers:
  tutorial_mickey   DistributedTutorialInterior.setup (Tutorial Tom's interior)
  quest_assign_101  DistributedNPCToon.setMovie QUEST_MOVIE_ASSIGN for quest 101 (Tom)
  quest_assign_110  ... for quest 110 (HQ Harry)
  tutorial_blocker  DistributedNPCBlocker.setMovie BLOCKER_MOVIE_START (Flippy at the tunnel)
  gag_intro         NewbiePurchaseManager.playMovie (the first trolley game's gag purchase,
                    which quest 110 sends the new toon to)
reward_100 has no caller upstream, so it is not generated.

Every command line becomes one ScriptCmd, in file order. Unknown commands, unknown variables
and wrong argument counts stop the generator: nothing is guessed. Dialogue variables (LOAD_DIALOGUE,
LOAD_CC_DIALOGUE) load nothing for an English client (QuestParser.parseLoadDialogue returns None
unless the language is Japanese), so the chat commands drop them.
"""
import argparse
import ast
import io
import os
import sys
import tokenize

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def first_existing(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return paths[0]


UPSTREAM = os.environ.get('TT_UPSTREAM') or first_existing(
    os.path.join(ROOT, 'ref', 'open-toontown', 'toontown'),
    os.path.join(ROOT, 'ref', 'ref', 'open-toontown', 'toontown'))
if os.path.basename(UPSTREAM) != 'toontown':
    UPSTREAM = os.path.join(UPSTREAM, 'toontown')
RESOURCES = os.environ.get('TT_RESOURCES') or first_existing(
    os.path.join(ROOT, 'ref', 'resources', 'phase_3'),
    os.path.join(ROOT, 'ref', 'ref', 'resources', 'phase_3'))
if os.path.basename(RESOURCES) == 'phase_3':
    RESOURCES = os.path.dirname(RESOURCES)
SCRIPT_FILE = os.path.join(RESOURCES, 'phase_3', 'etc', 'QuestScripts.txt')
QUEST_PARSER = os.path.join(UPSTREAM, 'quest', 'QuestParser.py')
LOCALIZER = os.path.join(UPSTREAM, 'toonbase', 'TTLocalizerEnglish.py')
OUT_H = os.path.join(ROOT, 'source', 'quest', 'quest_scripts.h')
OUT_CPP = os.path.join(ROOT, 'source', 'quest', 'quest_scripts.cpp')

# (script id, C++ enum name)
SCRIPTS = [
    ('tutorial_mickey', 'TutorialMickey'),
    ('quest_assign_101', 'QuestAssign101'),
    ('quest_assign_110', 'QuestAssign110'),
    ('tutorial_blocker', 'TutorialBlocker'),
    ('gag_intro', 'GagIntro'),
]

# The classic character that LOAD_CLASSIC_CHAR / CC_* pick: QuestParser.parseLoadClassicChar and
# parseCCChatConfirm use Mickey for a boy toon and Minnie for a girl.
CC_MALE = 'Mickey'
CC_FEMALE = 'Minnie'

# Variables that exist before a script runs (QuestParser.init globalVarDict, NPCMoviePlayer.__init__
# 'toon'/'npc', NewbiePurchaseManager.playMovie's purchase widgets).
GLOBAL_VARS = ['render', 'camera', 'hidden', 'aspect2d', 'localToon', 'laffMeter', 'inventory',
               'bFriendsList', 'book', 'bookPrevArrow', 'bookNextArrow', 'bookOpenButton',
               'bookCloseButton', 'chatNormalButton', 'chatScButton', 'arrows', 'toon', 'npc',
               'backToPlaygroundButton', 'playAgainButton', 'purchaseBg']

# Argument shapes, one letter per operand (QuestParser.parse* unpacking):
#   V variable   S string   N number   [..] optional tail   * chat extras (quit / CF flags / dialogue)
SHAPES = {
    'LOAD_SFX': 'VS', 'LOAD_DIALOGUE': 'VS', 'LOAD_CC_DIALOGUE': 'VS', 'LOAD': 'VS[S]',
    'LOAD_CLASSIC_CHAR': 'V', 'UNLOAD_CHAR': 'V', 'LOCK_LOCALTOON': '', 'FREE_LOCALTOON': '',
    'REPARENTTO': 'VV[S]', 'WRTREPARENTTO': 'VV[S]', 'SHOW': 'V', 'HIDE': 'V',
    'POS': 'VNNN', 'HPR': 'VNNN', 'SCALE': 'VNNN', 'POSHPRSCALE': 'VNNNNNNNNN', 'COLOR_SCALE': 'VNNNN',
    'WAIT': 'N', 'CLEAR_CHAT': 'V', 'CHAT_CONFIRM': 'VS*', 'LOCAL_CHAT_CONFIRM': 'VS*',
    'LOCAL_CHAT_PERSIST': 'VS*', 'CC_CHAT_CONFIRM': 'VS*', 'CC_CHAT_TO_CONFIRM': 'VVS*',
    'PLAY_SFX': 'V[N]', 'STOP_SFX': 'V', 'PLAY_ANIM': 'VS[N]', 'LOOP_ANIM': 'VS[N]',
    'LERP_POS': 'VNNNN', 'LERP_HPR': 'VNNNN', 'LERP_SCALE': 'VNNNN', 'LERP_POSHPRSCALE': 'VNNNNNNNNNN',
    'LERP_COLOR_SCALE': 'VNNNNNNNNN', 'FUNCTION': 'VS', 'ADD_LAFFMETER': 'N', 'LAFFMETER': 'NN',
    'OBSCURE_LAFFMETER': 'N', 'ARROWS_ON': 'NNNNNN', 'ARROWS_OFF': '', 'START_THROB': 'VNNNNNNNNN',
    'STOP_THROB': '', 'SHOW_BOOK': '', 'HIDE_BOOK': '', 'ENABLE_CLOSE_BOOK': '', 'OBSCURE_BOOK': 'N',
    'OBSCURE_CHAT': 'NN', 'ADD_INVENTORY': 'NNN', 'SET_INVENTORY': 'NNN', 'SET_INVENTORY_YPOS': 'NNN',
    'SET_INVENTORY_DETAIL': 'N[NN]', 'TOON_HEAD': 'VNNN[N]', 'SEND_EVENT': 'S', 'WAIT_EVENT': 'S',
    'BLACK_CAT_LISTEN': 'N', 'SHOW_THROW_SQUIRT_PREVIEW': '', 'SET_BIN': 'VS[N]', 'CLEAR_BIN': 'V',
    'SET_MUSIC_VOLUME': 'N[S[N[N]]]', 'DEBUG': 'S', 'FINISH_QUEST_MOVIE': '',
}
CHAT_COMMANDS = ('CHAT_CONFIRM', 'LOCAL_CHAT_CONFIRM', 'LOCAL_CHAT_PERSIST', 'CC_CHAT_CONFIRM',
                 'CC_CHAT_TO_CONFIRM')
# Commands QuestParser refuses inside UPON_TIMEOUT (they would wait).
NOT_UPON_TIMEOUT = ('WAIT', 'CHAT_CONFIRM', 'LOCAL_CHAT_CONFIRM', 'CC_CHAT_CONFIRM', 'CC_CHAT_TO_CONFIRM',
                    'WAIT_EVENT', 'SHOW_THROW_SQUIRT_PREVIEW')
FLAG_UPON_TIMEOUT = 1
FLAG_REVERSED = 2  # CFReversed: the speech balloon's tail points the other way


# Enum names where splitting on '_' would hide a word boundary.
OP_ENUM_NAMES = {
    'POSHPRSCALE': 'PosHprScale', 'LERP_POSHPRSCALE': 'LerpPosHprScale', 'REPARENTTO': 'ReparentTo',
    'WRTREPARENTTO': 'WrtReparentTo', 'LOCK_LOCALTOON': 'LockLocalToon', 'FREE_LOCALTOON': 'FreeLocalToon',
    'ADD_LAFFMETER': 'AddLaffMeter', 'LAFFMETER': 'LaffMeter', 'OBSCURE_LAFFMETER': 'ObscureLaffMeter',
    'SET_INVENTORY_YPOS': 'SetInventoryYPos', 'CC_CHAT_CONFIRM': 'CCChatConfirm',
    'CC_CHAT_TO_CONFIRM': 'CCChatToConfirm', 'LOAD_CC_DIALOGUE': 'LoadCCDialogue',
}


def op_enum(op):
    if op in OP_ENUM_NAMES:
        return OP_ENUM_NAMES[op]
    return ''.join(p[:1].upper() + p[1:].lower() for p in op.split('_'))


def var_enum(name):
    return name[:1].upper() + name[1:]


# ---------------------------------------------------------------- upstream tokenizer
def upstream_tokenizer():
    """QuestParser.getLineOfTokens, extracted with ast and executed unchanged."""
    with open(QUEST_PARSER, encoding='utf-8') as f:
        tree = ast.parse(f.read())
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'getLineOfTokens')
    module = ast.Module(body=[fn], type_ignores=[])

    class Notify:
        @staticmethod
        def warning(msg):
            raise SystemExit('QuestParser tokenizer warning: ' + msg)

    scope = {'tokenize': tokenize, 'notify': Notify}
    exec(compile(module, QUEST_PARSER, 'exec'), scope)
    return scope['getLineOfTokens']


def read_scripts(path=SCRIPT_FILE):
    """{script id: [token lines]} exactly as QuestParser.readFile builds lineDict."""
    get_line = upstream_tokenizer()
    with open(path, 'rb') as f:
        data = f.read()
    gen = tokenize.tokenize(io.BytesIO(data).readline)
    scripts = {}
    cur = None
    line = get_line(gen)
    while line is not None:
        if line == []:
            line = get_line(gen)
            continue
        if line[0] == 'ID':
            cur = line[1]
            if cur in scripts:
                raise SystemExit('duplicate script id ' + cur)
            scripts[cur] = []
        elif cur is None:
            raise SystemExit('every script must begin with an ID')
        else:
            scripts[cur].append(line)
        line = get_line(gen)
    return scripts


# ---------------------------------------------------------------- localizer
def localizer_strings(path=LOCALIZER):
    """Module-level NAME = 'literal' assignments of TTLocalizerEnglish (the chat strings are all
    plain literals)."""
    with open(path, encoding='utf-8') as f:
        tree = ast.parse(f.read())
    out = {}
    for n in tree.body:
        if isinstance(n, ast.Assign) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str):
            for t in n.targets:
                if isinstance(t, ast.Name):
                    out[t.id] = n.value.value
    return out


# ---------------------------------------------------------------- one command
class Cmd:
    def __init__(self):
        self.op = ''
        self.flags = 0
        self.var = None
        self.var2 = None
        self.quit = 0
        self.key = None
        self.text = None
        self.text_alt = None
        self.str2 = None
        self.args = []

    def canonical(self):
        """One line, shared with tools/tutorial_test (the C++ dump prints the same)."""
        parts = []
        if self.flags & FLAG_UPON_TIMEOUT:
            parts.append('UPON_TIMEOUT')
        parts.append(self.op)
        for v in (self.var, self.var2):
            if v:
                parts.append(v)
        for s in (self.key, self.text, self.text_alt, self.str2):
            if s is not None:
                parts.append(repr(s))
        if self.op in CHAT_COMMANDS:
            parts.append('quit=%d' % self.quit)
            parts.append('reversed=%d' % (1 if self.flags & FLAG_REVERSED else 0))
        parts.extend('%.6g' % a for a in self.args)
        return ' '.join(parts)


def take_shape(op, operands, shape):
    """Split operands by shape; returns (vars, strings, numbers, chat extras)."""
    out_v, out_s, out_n = [], [], []
    i = 0
    optional = False
    pos = 0
    chat_extra = None
    while pos < len(shape):
        c = shape[pos]
        pos += 1
        if c == '[':
            optional = True
            continue
        if c == ']':
            continue
        if c == '*':
            chat_extra = operands[i:]
            i = len(operands)
            continue
        if i >= len(operands):
            if optional:
                break
            raise SystemExit('%s: missing operands (%r)' % (op, operands))
        tok = operands[i]
        i += 1
        if c == 'V':
            if not isinstance(tok, str):
                raise SystemExit('%s: expected a variable, got %r' % (op, tok))
            out_v.append(tok)
        elif c == 'S':
            if not isinstance(tok, str):
                raise SystemExit('%s: expected a string, got %r' % (op, tok))
            out_s.append(tok)
        elif c == 'N':
            if isinstance(tok, bool) or not isinstance(tok, (int, float)):
                raise SystemExit('%s: expected a number, got %r' % (op, tok))
            out_n.append(float(tok))
    if i != len(operands):
        raise SystemExit('%s: too many operands (%r)' % (op, operands))
    return out_v, out_s, out_n, chat_extra


def build_script(sid, lines, strings):
    defined = set(GLOBAL_VARS)
    dialogue = set()
    cmds = []
    for line in lines:
        tokens = list(line)
        cmd = Cmd()
        if tokens[0] == 'UPON_TIMEOUT':
            cmd.flags |= FLAG_UPON_TIMEOUT
            tokens = tokens[1:]
            if tokens[0] in NOT_UPON_TIMEOUT:
                raise SystemExit('%s: %s not allowed in an UPON_TIMEOUT' % (sid, tokens[0]))
        op = tokens[0]
        if op not in SHAPES:
            raise SystemExit('%s: unknown command %s' % (sid, op))
        cmd.op = op
        vars_, strs, nums, extra = take_shape(op, tokens[1:], SHAPES[op])
        # Variables a command defines.
        if op in ('LOAD_SFX', 'LOAD', 'LOAD_CLASSIC_CHAR'):
            defined.add(vars_[0])
        elif op in ('LOAD_DIALOGUE', 'LOAD_CC_DIALOGUE'):
            defined.add(vars_[0])
            dialogue.add(vars_[0])
        elif op == 'TOON_HEAD':
            defined.add(vars_[0] + 'ToonHead')  # parseToonHead: setVar('%sToonHead' % toonName)
        for v in vars_:
            if v not in defined:
                raise SystemExit('%s: variable not defined: %s' % (sid, v))
        if vars_:
            cmd.var = vars_[0]
        if len(vars_) > 1:
            cmd.var2 = vars_[1]
        if op in CHAT_COMMANDS:
            key = strs[0]
            for e in extra:
                if isinstance(e, int) and not isinstance(e, bool):
                    cmd.quit = e
                elif isinstance(e, str) and len(e) > 2 and e[:2] == 'CF':
                    if e != 'CFReversed':
                        raise SystemExit('%s: unsupported chat flag %s' % (sid, e))
                    cmd.flags |= FLAG_REVERSED
                elif isinstance(e, str) and e in dialogue:
                    pass  # English: no dialogue audio (parseLoadDialogue)
                else:
                    raise SystemExit('%s: invalid chat argument %r' % (sid, e))
            cmd.key = key
            if op.startswith('CC_'):
                male = strings.get(key % CC_MALE)
                female = strings.get(key % CC_FEMALE)
                if male is None or female is None:
                    raise SystemExit('%s: no TTLocalizer string for %s' % (sid, key))
                if op == 'CC_CHAT_TO_CONFIRM':
                    # parseCCChatToConfirm: '%s' becomes TTLocalizer.<toAvatar name>; the target is
                    # the classic character (Mickey for a boy toon, Minnie for a girl).
                    if cmd.var2 != 'classicChar':
                        raise SystemExit('%s: CC_CHAT_TO_CONFIRM to %s' % (sid, cmd.var2))
                    male = male.replace('%s', strings[CC_MALE])
                    female = female.replace('%s', strings[CC_FEMALE])
                cmd.text = male
                cmd.text_alt = female
            else:
                if key not in strings:
                    raise SystemExit('%s: no TTLocalizer string for %s' % (sid, key))
                cmd.text = strings[key]
        else:
            if strs:
                cmd.text = strs[0]
            if len(strs) > 1:
                cmd.str2 = strs[1]
            cmd.args = nums
        cmds.append(cmd)
    return cmds


def build_all():
    scripts = read_scripts()
    strings = localizer_strings()
    out = []
    for sid, enum in SCRIPTS:
        if sid not in scripts:
            raise SystemExit('QuestScripts.txt has no ID ' + sid)
        out.append((sid, enum, build_script(sid, scripts[sid], strings)))
    return out


# ---------------------------------------------------------------- C++ output
def c_str(s):
    if s is None:
        return 'nullptr'
    out = []
    for ch in s:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\x07':
            out.append('\\a')  # the chat page delimiter (QuestScripts.txt header)
        elif ord(ch) < 32 or ord(ch) > 126:
            out.append('\\x%02x""' % ord(ch) if ord(ch) < 256 else ch)
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def c_float(v):
    s = repr(float(v))
    if 'e' in s or 'E' in s:
        return s + 'f'
    return s + 'f'


def emit(scripts):
    ops = sorted(SHAPES)
    used_vars = []
    for _, _, cmds in scripts:
        for c in cmds:
            for v in (c.var, c.var2):
                if v and v not in used_vars:
                    used_vars.append(v)
    used_vars.sort(key=str.lower)
    max_args = max(len(c.args) for _, _, cmds in scripts for c in cmds)

    h = []
    h.append('// GENERATED by tools/gen_quest_scripts.py from phase_3/etc/QuestScripts.txt. Do not edit.')
    h.append('// The Toontorial scenes, one ScriptCmd per script line (docs/design/tutorial.md section 3).')
    h.append('// Played by quest/script_player.* (QuestParser.NPCMoviePlayer semantics).')
    h.append('#pragma once')
    h.append('')
    h.append('#include <cstdint>')
    h.append('')
    h.append('namespace tt::quest {')
    h.append('')
    h.append('enum class ScriptId : uint8_t {')
    for sid, enum, _ in scripts:
        h.append('    %s, // %s' % (enum, sid))
    h.append('    Count,')
    h.append('};')
    h.append('')
    h.append('// QuestParser commands (NPCMoviePlayer.play), in alphabetical order.')
    h.append('enum class ScriptOp : uint8_t {')
    for op in ops:
        h.append('    %s, // %s' % (op_enum(op), op))
    h.append('    Count,')
    h.append('};')
    h.append('')
    h.append('// Script variables (QuestParser globalVarDict, NPCMoviePlayer private vars).')
    h.append('enum class ScriptVar : uint8_t {')
    h.append('    None,')
    for v in used_vars:
        h.append('    %s, // %s' % (var_enum(v), v))
    h.append('    Count,')
    h.append('};')
    h.append('')
    h.append('constexpr uint8_t kScriptUponTimeout = %d; // UPON_TIMEOUT: run only when the movie is cut short' %
             FLAG_UPON_TIMEOUT)
    h.append('constexpr uint8_t kScriptReversed = %d;    // chat flag CFReversed' % FLAG_REVERSED)
    h.append('constexpr int kScriptMaxArgs = %d;' % max_args)
    h.append('')
    h.append('struct ScriptCmd {')
    h.append('    ScriptOp op;')
    h.append('    uint8_t flags;       // kScriptUponTimeout | kScriptReversed')
    h.append('    ScriptVar var;       // first variable operand')
    h.append('    ScriptVar var2;      // second (REPARENTTO parent, CC_CHAT_TO_CONFIRM target)')
    h.append('    int8_t quit;         // chat: quitButton (1 = the last page closes the dialog)')
    h.append('    uint8_t numArgs;     // numbers in args[]')
    h.append('    const char* key;     // chat: the TTLocalizer name')
    h.append('    const char* text;    // chat text (boy toon for CC_*), or the command\'s string operand')
    h.append('    const char* textAlt; // CC_* chat text for a girl toon (Minnie); nullptr otherwise')
    h.append('    const char* str2;    // second string operand (LOAD sub-node, REPARENTTO node path)')
    h.append('    float args[kScriptMaxArgs];')
    h.append('};')
    h.append('')
    h.append('struct ScriptDef {')
    h.append('    const char* name; // QuestScripts.txt ID')
    h.append('    const ScriptCmd* cmds;')
    h.append('    int numCmds;')
    h.append('};')
    h.append('')
    h.append('const ScriptDef& scriptDef(ScriptId id);')
    h.append('bool findScript(const char* name, ScriptId* out);')
    h.append('const char* scriptOpName(ScriptOp op);   // "LOCAL_CHAT_CONFIRM"')
    h.append('const char* scriptVarName(ScriptVar v);  // "npc"; "" for None')
    h.append('')
    h.append('} // namespace tt::quest')
    h.append('')

    c = []
    c.append('// GENERATED by tools/gen_quest_scripts.py from phase_3/etc/QuestScripts.txt. Do not edit.')
    c.append('#include "quest/quest_scripts.h"')
    c.append('')
    c.append('#include <cstring>')
    c.append('')
    c.append('namespace tt::quest {')
    c.append('')
    c.append('namespace {')
    c.append('')
    for sid, enum, cmds in scripts:
        c.append('// ID %s' % sid)
        c.append('const ScriptCmd k%s[] = {' % enum)
        for cmd in cmds:
            args = ', '.join(c_float(a) for a in cmd.args)
            c.append('    {ScriptOp::%s, %d, ScriptVar::%s, ScriptVar::%s, %d, %d, %s, %s, %s, %s, {%s}},' % (
                op_enum(cmd.op), cmd.flags, var_enum(cmd.var) if cmd.var else 'None',
                var_enum(cmd.var2) if cmd.var2 else 'None', cmd.quit, len(cmd.args), c_str(cmd.key),
                c_str(cmd.text), c_str(cmd.text_alt), c_str(cmd.str2), args))
        c.append('};')
        c.append('')
    c.append('const ScriptDef kDefs[] = {')
    for sid, enum, cmds in scripts:
        c.append('    {"%s", k%s, %d},' % (sid, enum, len(cmds)))
    c.append('};')
    c.append('static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == static_cast<int>(ScriptId::Count), "script table");')
    c.append('')
    c.append('const char* const kOpNames[] = {')
    for op in ops:
        c.append('    "%s",' % op)
    c.append('};')
    c.append('static_assert(sizeof(kOpNames) / sizeof(kOpNames[0]) == static_cast<int>(ScriptOp::Count), "op names");')
    c.append('')
    c.append('const char* const kVarNames[] = {')
    c.append('    "",')
    for v in used_vars:
        c.append('    "%s",' % v)
    c.append('};')
    c.append('static_assert(sizeof(kVarNames) / sizeof(kVarNames[0]) == static_cast<int>(ScriptVar::Count), "var names");')
    c.append('')
    c.append('} // namespace')
    c.append('')
    c.append('const ScriptDef& scriptDef(ScriptId id) {')
    c.append('    return kDefs[static_cast<int>(id)];')
    c.append('}')
    c.append('')
    c.append('bool findScript(const char* name, ScriptId* out) {')
    c.append('    for (int i = 0; i < static_cast<int>(ScriptId::Count); i++) {')
    c.append('        if (!strcmp(kDefs[i].name, name)) {')
    c.append('            *out = static_cast<ScriptId>(i);')
    c.append('            return true;')
    c.append('        }')
    c.append('    }')
    c.append('    return false;')
    c.append('}')
    c.append('')
    c.append('const char* scriptOpName(ScriptOp op) {')
    c.append('    return kOpNames[static_cast<int>(op)];')
    c.append('}')
    c.append('')
    c.append('const char* scriptVarName(ScriptVar v) {')
    c.append('    return kVarNames[static_cast<int>(v)];')
    c.append('}')
    c.append('')
    c.append('} // namespace tt::quest')
    c.append('')
    return '\n'.join(h), '\n'.join(c)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='exit 1 if the generated files are stale')
    ap.add_argument('--dump', action='store_true', help='print the canonical form of every command')
    args = ap.parse_args()
    scripts = build_all()
    if args.dump:
        for sid, _, cmds in scripts:
            for i, cmd in enumerate(cmds):
                print('%s %d %s' % (sid, i, cmd.canonical()))
        return 0
    header, source = emit(scripts)
    if args.check:
        stale = []
        for path, text in ((OUT_H, header), (OUT_CPP, source)):
            try:
                with open(path, encoding='utf-8') as f:
                    if f.read() != text:
                        stale.append(path)
            except FileNotFoundError:
                stale.append(path)
        for p in stale:
            print('stale: ' + os.path.relpath(p, ROOT))
        return 1 if stale else 0
    for path, text in ((OUT_H, header), (OUT_CPP, source)):
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
    total = sum(len(cmds) for _, _, cmds in scripts)
    print('wrote %d scripts (%d commands) to %s' % (len(scripts), total, os.path.relpath(OUT_CPP, ROOT)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
