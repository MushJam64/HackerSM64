#include "game/puppycam2.h"
///This is the bit that defines where the angles happen. They're basically environment boxes that dictate camera behaviour.
///Permaswap is a boolean that simply determines wether or not when the camera changes at this point it stays changed. 0 means it resets when you leave, and 1 means it stays changed.
///The camera position fields accept "32767" as an ignore flag.
///The script supports anything that does not take an argument. It's reccomended to keep the scripts in puppycam_scripts.inc.c for the sake of cleanliness.
///If you do not wish to use a script in the angle, then just leave the field as 0.

// #define PUPPYCAM_SAMPLES

struct newcam_hardpos newcam_fixedcam[] =
{

	{LEVEL_WMOTR, 1, 1, NC_MODE_FIXED_NOMOVE, 0, -939, -1544, -939, 939, 456, 939, -22, 245, 823, -22, 233, 723},

};