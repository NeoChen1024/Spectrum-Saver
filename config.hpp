/* config.h: miscellaneous configurations */

/* options used by spsave: */

// tinySA Zero Level, 128 for tinySA, 174 for tinySA Ultra
const static int ZERO_LEVEL = 128;
const static int  ZERO_LEVEL_ULTRA = 174;

/* options used by log2png: */

// Font for info text
#define FONT_FAMILY	"Iosevka Term Slab"

// Height of banner and footer in pixels
#define BANNER_HEIGHT	64
#define BANNER_COLOR	"white"

#define FOOTER_HEIGHT	24
#define FOOTER_COLOR	"yellow"
#define PX_TO_PT(x)	((double)(x) * 72 / 96)

// Also used by spsave to determine when to rotate log file
// 1440 records would be 24 hours at 1 minute interval
#define MAX_RECORDS	1440 
