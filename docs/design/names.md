# Name Shop, Typed Names, and Avatar Management Design

Status: design specification for the Name Shop and avatar management on Nintendo 3DS.
Reference paths are relative to `ref/open-toontown/` unless specified.

## 1. Upstream Architecture & Flow

In original Disney Toontown and Open Toontown, avatar creation and naming are managed across:
1. `toontown/makeatoon/MakeAToon.py` — Orchestrates character creation steps (Gender, Body, Color, Clothes, Name).
2. `toontown/makeatoon/NameShop.py` — The GUI for selecting or typing a name.
3. `toontown/makeatoon/NameGenerator.py` — Parses `phase_3/etc/NameMasterEnglish.txt`, provides categorized lists and random name generation.
4. `otp/namepanel/PickANamePattern.py` & `toontown/makeatoon/TTPickANamePattern.py` — Encodes and decodes pattern tuples `[(p1, f1), (p2, f2), (p3, f3), (p4, f4)]` with prefix capitalization rules.
5. `otp/namepanel/NameCheck.py` — Client-side validation for typed names (16 rule checks).
6. `otp/login/AstronLoginManagerUD.py` — Server-side handling of avatar creation, pattern naming, typed name moderation/judging, name acknowledgment, and avatar deletion.
7. `toontown/login/AvatarChooser.py` & `AvatarChoice.py` — Pick-a-Toon UI showing name review/approval badges, name acknowledgment, and avatar deletion.

---

## 2. Astron Protocol Messages (`AstronLoginManager`)

All messages are handled by `AstronLoginManager` (doId 4670, `etc/otp.dc:525-540`):

```
struct PotentialAvatar {
    uint32 avNum;
    string avName;
    blob avDNA;
    uint8 avPosition;
    uint8 nameState;
};

dclass AstronLoginManager : DistributedObject {
    requestLogin(string) clsend;
    loginResponse(blob);
    requestAvatarList() clsend;
    avatarListResponse(PotentialAvatar[]);
    createAvatar(blob, uint8) clsend;
    createAvatarResponse(uint32);
    setNamePattern(uint32, int16, uint8, int16, uint8, int16, uint8, int16, uint8) clsend;
    namePatternAnswer(uint32, uint8);
    setNameTyped(uint32, string) clsend;
    nameTypedResponse(uint32, uint8);
    acknowledgeAvatarName(uint32) clsend;
    acknowledgeAvatarNameResponse();
    requestRemoveAvatar(uint32) clsend;
    requestPlayAvatar(uint32) clsend;
};
```

### 2.1 Creation & Default Naming
- `createAvatar(blob dna, uint8 slot) clsend`
  - Upstream (`AstronLoginManagerUD.py:464-474`): creates the database object `DistributedToonUD` with:
    - `setName`: `"<Color> <Species>"` (e.g., "Sea Green Dog")
    - `WishNameState`: `'OPEN'`
    - `WishName`: `''`
  - Server replies `createAvatarResponse(uint32 avId)`.

### 2.2 Pick-a-Name (`setNamePattern`)
- `setNamePattern(uint32 avId, int16 p1, uint8 f1, int16 p2, uint8 f2, int16 p3, uint8 f3, int16 p4, uint8 f4) clsend`
  - Parameters:
    - `p1, f1`: Title word index (-1 if omitted), capitalization flag (1 = capitalize, 0 = lowercase).
    - `p2, f2`: First name index (-1 if omitted), capitalization flag (0 for 'von', else 1).
    - `p3, f3`: Last name prefix index (-1 if omitted), capitalization flag (1).
    - `p4, f4`: Last name suffix index (-1 if omitted), capitalization flag (1 if prefix is "Mc" or "Mac", else 0).
  - Server logic (`AstronLoginManagerUD.py:550-575`):
    - Looks up each non-negative index in `nameDictionary`.
    - Capitalizes or lowercases based on `f`.
    - Concatenates prefix and suffix without space: `parts[2] += parts.pop(3)`.
    - Joins remaining parts with space: `' '.join(parts)`.
    - Updates DB: `WishNameState = ('LOCKED',)`, `WishName = ('',)`, `setName = (name,)`.
    - Sends `namePatternAnswer(avId, 1)` to the account.

### 2.3 Typed Names (`setNameTyped`)
- `setNameTyped(uint32 avId, string name) clsend`
  - Used either during Make-a-Toon or from Pick-a-Toon ("Name Your Toon").
  - If `avId == 0`, acts as a dry-run check.
  - Server judging behavior (`AstronLoginManagerUD.py:623-633`):
    ```python
    def __handleJudgeName(self):
        status = 1  # 1 = PENDING, 2 = APPROVED, 0 = REJECTED
        if self.avId and status:
            self.loginManager.air.dbInterface.updateObject(
                ..., {'WishNameState': ('PENDING',), 'WishName': (self.name,)})
        self.loginManager.sendUpdateToAccountId(self.sender, 'nameTypedResponse', [self.avId, status])
    ```
  - In stock Open Toontown, all typed names are accepted for moderation with status `1` (`PENDING`). The avatar's displayed name in `setName` remains the default color+species name while under review.
  - Server sends `nameTypedResponse(avId, 1)` back to client.

### 2.4 Name States (`PotentialAvatar.nameState`)
When `requestAvatarList` or avatar updates occur, the server maps `WishNameState` to numeric `nameState` (`AstronLoginManagerUD.py:385-403`):
- `0 = Locked` (`WishNameState == 'LOCKED'`): Finalized name, no further naming allowed.
- `1 = Open` (`WishNameState == 'OPEN'`): Default color+species name; can choose a pattern name or type a name.
- `2 = Pending` (`WishNameState == 'PENDING'`): Typed name is submitted and awaiting review.
- `3 = Approved` (`WishNameState == 'APPROVED'`): Moderation approved the name. Note: server replaces `PotentialAvatar.avName` with `fields['WishName']`.
- `4 = Rejected` (`WishNameState == 'REJECTED'`): Moderation rejected the name.

### 2.5 Acknowledging Names (`acknowledgeAvatarName`)
- `acknowledgeAvatarName(uint32 avId) clsend`
  - Handled in `AstronLoginManagerUD.py:660-685`:
    - If `WishNameState == 'APPROVED'`:
      - `WishNameState` becomes `'LOCKED'`.
      - `setName` becomes `WishName`.
      - `WishName` cleared to `''`.
    - If `WishNameState == 'REJECTED'`:
      - `WishNameState` reset to `'OPEN'`.
      - `WishName` cleared to `''`.
  - Server sends `acknowledgeAvatarNameResponse()`.
  - Client updates its local avatar state accordingly.

### 2.6 Avatar Deletion (`requestRemoveAvatar`)
- `requestRemoveAvatar(uint32 avId) clsend`
  - Handled in `AstronLoginManagerUD.py:700-740`:
    - Checks `avId` belongs to the account.
    - Clears the avatar from `ACCOUNT_AV_SET` (replaces with 0).
    - Appends `[avId, timestamp]` to `ACCOUNT_AV_SET_DEL`.
    - If account has an estate, zeroes out the toon's house/slot on the estate.
    - Upon database completion, automatically re-queries the remaining avatars and sends a fresh `avatarListResponse(PotentialAvatar[])`.

---

## 3. Name Master Tables & Categories

From `ref/ref/resources/phase_3/etc/NameMasterEnglish.txt`:
- Contiguous table of 435 entries (IDs 0..434).
- Categories:
  - `0`: Boy Title (7 entries: Baron, Duke, King, Master, Mister, Prince, Sir)
  - `1`: Girl Title (6 entries: Lady, Miss, Princess, Queen, Granny, Aunt)
  - `2`: Neutral Title (20 entries: Captain, Colonel, Doctor, Prof., Sheriff, etc.)
  - `3`: Boy First Name (65 entries: Bob, Mickey, Donald, Flippy, etc.)
  - `4`: Girl First Name (38 entries: Daisy, Minnie, Penelope, Roxy, etc.)
  - `5`: Neutral First Name (106 entries: Binky, Boots, Buster, Clover, Pepper, etc.)
  - `6`: Capitalized Last Prefix (2 entries: "Mc" [id 242], "Mac" [id 243])
  - `7`: Last Prefix (94 entries: Blue, Crumb, Feather, Fuzzy, Super, etc.)
  - `8`: Last Suffix (97 entries: bee, blabber, bottom, clown, noodle, paw, etc.)

Gender mappings:
- Male: Titles = {0, 2}, Firsts = {3, 5}, Prefixes = {6, 7}, Suffixes = {8}.
- Female: Titles = {1, 2}, Firsts = {4, 5}, Prefixes = {6, 7}, Suffixes = {8}.

Pattern Randomization:
- Upstream selects combination `uberFlag`: `'title-first'`, `'title-last'`, `'first'`, `'last'`, `'first-last'`, `'title-first-last'`.
- Part capitalization flag `f4`: 1 if prefix is in Category 6 ("Mc" or "Mac"), else 0.

---

## 4. Typed Name Validation (`NameCheck`)

Upstream `otp/namepanel/NameCheck.py` enforces 16 checks in order:
1. `printableChars`: ASCII characters must be printable (`NCGeneric`).
2. `badCharacters`: Only `.,'-` plus letters and spaces. Digits trigger `NCNoDigits`. Other characters trigger `NCBadCharacter`.
3. `fontHasCharacters`: Characters supported by interface font.
4. `longEnough`: Length >= 2 (`NCTooShort`).
5. `emptyName`: Strip whitespace != "" (`NCTooShort`).
6. `hasLetters`: Every word (split by space and hyphen) must contain at least one letter (`NCNeedLetters`).
7. `hasVowels`: In words without periods and length > 2, must contain at least one vowel `[aeiouyAEIOUY]` (`NCNeedVowels`).
8. `monoLetter`: Words with > 2 letters cannot consist of a single repeated letter like "aaa" (`NCGeneric`).
9. `checkDashes`: Hyphen cannot be at start/end of name, and must have letters immediately before and after (`NCDashUsage`).
10. `checkCommas`: Comma cannot be at start/end (`NCCommaEdge`), cannot be preceded by space (`NCCommaAfterWord`), and must be followed by space (`NCCommaUsage`).
11. `checkPeriods`: Periods must be at end of word; max 2 periods per word; 2 periods must match "X.Y." pattern like "J.T." (`NCPeriodUsage`).
12. `checkApostrophes`: Max 2 apostrophes per word, max 3 apostrophes in total name (`NCApostrophes`).
13. `tooManyWords`: Max 4 words (`NCTooManyWords`).
14. `allCaps`: If > 2 letters, name cannot be entirely uppercase (`NCAllCaps`).
15. `mixedCase`: Words > 2 chars cannot have > 2 capital letters (`NCMixedCase`).
16. `repeatedChars`: No character may be repeated 3 or more times consecutively (`NCRepeatedChar`).

---

## 5. Nintendo 3DS Implementation

### 5.1 Keyboard (`swkbd`)
- Uses `libctru` `swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 32)`.
- Filters invalid input and checks client-side `NameCheck`.
- In autotest mode, a test hook string bypasses the blocking UI keyboard applet.

### 5.2 Make-a-Toon UI
- Step 4 (Naming):
  - Pick-a-Name columns: Title, First, Prefix, Suffix.
  - Active toggle buttons for each part.
  - `<` and `>` buttons to cycle choices.
  - "Shuffle" button to randomize combination and choices according to gender rules.
  - "Type Name" button: launches `swkbd` (or test hook), validates with `NameCheck`, displays result.
  - On "Create!", sends `createAvatar(dna, slot)`. When `createAvatarResponse(avId)` is received, immediately sends `setNamePattern` (if pattern) or `setNameTyped` (if typed), then enters playground.

### 5.3 Pick-a-Toon UI
- Slot badges:
  - `Open`: "Name Me" (green badge)
  - `Pending`: "Review" (orange badge)
  - `Approved`: "Approved" (green badge)
  - `Rejected`: "Rejected" (red badge)
- "Acknowledge" button: visible when selected toon is `Approved` or `Rejected`. Tapping sends `acknowledgeAvatarName(avId)`.
- "Delete" button: visible for any selected toon. Tapping prompts confirmation modal ("Delete <name>? Cancel / Delete"). Confirming sends `requestRemoveAvatar(avId)`.
