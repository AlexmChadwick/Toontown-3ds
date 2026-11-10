"""Groups UI strings (English). One module so the Shticker Book patch needs no localizer edit."""
PageTitle = 'Groups'
TabTitle = 'Groups'
KindNames = ('Boss Battle', 'Cog Building', 'Toon Task')
KindButtons = ('Boss\nBattle', 'Cog\nBuilding', 'Toon\nTask')
KindShort = ('Boss', 'Building', 'Task')
DepartmentNames = ('Sellbot', 'Cashbot', 'Lawbot', 'Bossbot')
TaskNames = ('Toontown Central', "Donald's Dock", 'The Brrrgh', "Minnie's Melodyland",
             'Daisy Gardens', "Donald's Dreamland")
# hood ids of the TaskNames, in order
TaskHoods = (2000, 1000, 3000, 4000, 5000, 9000)
CreateTitle = 'Start a group'
PickTarget = 'Pick one'
Create = 'Create'
Join = 'Join'
Leave = 'Leave'
Full = 'Full'
Mine = 'Your group'
InProgress = 'Started'
Members = '%d/%d'
EmptyList = 'No groups yet. Start one with Boss, Building or Task.'
Connecting = 'Looking for groups.'
Unavailable = 'Groups are not available right now.'
AlreadyHere = 'You are already here!'
GoTo = 'Go to %s to meet your group.'
Joined = 'You joined the group!'
Created = 'Your group is open!'
Left = 'You left the group.'
Errors = {1: 'Something went wrong. Try again.', 2: 'You are already in a group.',
          3: 'That group is gone.', 4: 'That group is full.', 5: 'Please wait a moment.',
          6: 'You are not in a group.'}
Back = 'Back'
BadgeHint = 'New group!'
NoPlace = 'a playground'


def group_title(kind, target):
    if kind == 2:
        return KindNames[2]
    return '%s %s' % (DepartmentNames[target], KindNames[kind])


def task_place(target):
    return TaskNames[target] if 0 <= target < len(TaskNames) else NoPlace
