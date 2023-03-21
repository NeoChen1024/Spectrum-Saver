/* config.h: miscellaneous configurations */

/* options used by spsave: */

// 1440 records would be 24 hours at 1 minute interval
#define MAX_RECORDS	1440 

// tinySA Zero Level, 128 for tinySA, 174 for tinySA Ultra
constexpr static int ZERO_LEVEL =	128;
constexpr static int ZERO_LEVEL_ULTRA =	174;

/* options used by log2png: */

// Font for info text
// Too long, can't be constexpr
constexpr static string FONT_FAMILY{"Iosevka Term"};

// Height of banner and footer in pixels
constexpr static int BANNER_HEIGHT{64};
constexpr static int FOOTER_HEIGHT{24};
constexpr static string BANNER_COLOR{"white"};
constexpr static string FOOTER_COLOR{"yellow"};

#define PX_TO_PT(x)	((double)(x) * 72 / 96)

// Minimum number of gridlines to draw
constexpr static int MIN_GRIDLINES = 6;
