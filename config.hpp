/* config.h: miscellaneous configurations */

/* options used by spsave: */

// tinySA Zero Level, 128 for tinySA, 174 for tinySA Ultra
constexpr static int ZERO_LEVEL =	128;
constexpr static int ZERO_LEVEL_ULTRA =	174;

/* options used by log2png: */

// Font for info text
// Too long, can't be constexpr
const static string FONT_FAMILY{"Iosevka Term"};

// Height of banner and footer in pixels
constexpr static int BANNER_HEIGHT = 64;
constexpr static int FOOTER_HEIGHT = 24;
const static string BANNER_COLOR{"white"};
const static string FOOTER_COLOR{"yellow"};

#define PX_TO_PT(x)	((double)(x) * 72 / 96)

// Minimum number of gridlines to draw
constexpr static int MIN_GRIDLINES = 6;
