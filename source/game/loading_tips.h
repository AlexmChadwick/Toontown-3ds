// ToontownLoadingScreen's TOON TIP (TTLocalizer.TipTitle + TipDict), shown on the front end's
// connecting / heading-to screens. Verbatim upstream tips from TIP_GENERAL and TIP_STREET, minus
// the ones that name PC keys ("End", "F8", "Control", ...) or features this server leaves as stubs
// (estates, the Cattlelog, Doodle adoption, fish sales, friends lists).
#pragma once

namespace tt::game {

constexpr const char* kTipTitle = "TOON TIP:";
constexpr const char* kLoadingTips[] = {
    // TIP_GENERAL
    "Every time you turn in a completed ToonTask your Laff points are automatically refilled.",
    "Rewards for some ToonTasks allow you to carry more gags and jellybeans.",
    "Some ToonTask rewards let you teleport to playgrounds in Toontown by using the Map Page in the Shticker Book.",
    "Increase your Laff points in the Playgrounds by collecting treasures like stars and ice cream cones.",
    "Sometimes you can find several different ToonTasks offered for the same reward. Shop around!",
    "Finding friends with similar ToonTasks is a fun way to progress through the game.",
    "You never need to save your Toontown progress. The Toontown servers continually save all the necessary "
    "information.",
    "Some SpeedChat phrases play emotion animations on your Toon.",
    "If the area you are in is crowded, try changing Districts. Go to the District Page in the Shticker Book and "
    "select a different one.",
    "Enter Goofy Speedway through the tire-shaped tunnel in Toontown Central Playground.",
    // TIP_STREET
    "There are four types of Cogs: Lawbots, Cashbots, Sellbots, and Bossbots.",
    "Each Gag Track has different amounts of accuracy and damage.",
    "Sound gags will affect all Cogs but will wake up any lured Cogs.",
    "Defeating Cogs in strategic order can greatly increase your chances of winning battles.",
    "The Toon-Up Gag Track lets you heal other Toons in battle.",
    "Multiple Toons can team up and use the same Gag Track in battle to get bonus Cog damage.",
    "In battle, gags are used in order from top to bottom as displayed on the Gag Menu.",
};
constexpr int kLoadingTipCount = int(sizeof(kLoadingTips) / sizeof(kLoadingTips[0]));

} // namespace tt::game
