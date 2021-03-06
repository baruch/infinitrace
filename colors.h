#ifndef __COLORS_H__
#define __COLORS_H__

#define GREY            "\033[1;24;30m"
#define WHITE           "\033[0;37m"
#define GREEN           "\033[0;32m"
#define CYAN            "\033[0;36m"
#define YELLOW          "\033[0;33m"
#define MAGENTA         "\033[0;35m"
#define WHITE_B         "\033[1;37m" 
#define GREEN_B         "\033[1;32m" 
#define YELLOW_B        "\033[1;33m"
#define RED_B           "\033[1;31m"
#define CYAN_B          "\033[1;36m"
#define BLUE_B          "\033[1;34m"
#define MAGENTA_B       "\033[1;35m"
#define ANSI_RESET      "\033[0;39;49m"


#define _B_BLACK(x)        "\033[40m" x
#define _F_GREY(x)         "\033[1;24;30m" x
#define _F_WHITE(x)        "\033[0;37m" x
#define _F_CYAN(x)    	   CYAN x
#define _F_GREEN(x)        "\033[0;32m" x
#define _F_YELLOW(x)  	   YELLOW x
#define _F_MAGENTA(x)      "\033[0;35m" x
#define _F_WHITE_BOLD(x)   "\033[1;37m" x 
#define _F_GREEN_BOLD(x)   "\033[1;32m" x 
#define _F_YELLOW_BOLD(x)  "\033[1;33m" x
#define _F_RED_BOLD(x)     "\033[1;31m" x
#define _F_CYAN_BOLD(x)    "\033[1;36m" x
#define _F_BLUE_BOLD(x)    "\033[1;34m" x
#define _F_MAGENTA_BOLD(x) "\033[1;35m" x
#define _ANSI_DEFAULTS(x)  "\033[0;39;49m" x


#define B_BLACK(x)        COLOR_BOOL ? _B_BLACK(x) : x
#define F_GREY(x)         COLOR_BOOL ? _F_GREY(x) : x
#define F_WHITE(x)        COLOR_BOOL ? _F_WHITE(x) : x
#define F_GREEN(x)        COLOR_BOOL ? _F_GREEN(x) : x
#define F_MAGENTA(x)      COLOR_BOOL ? _F_MAGENTA(x) : x
#define F_WHITE_BOLD(x)   COLOR_BOOL ? _F_WHITE_BOLD(x) : x
#define F_GREEN_BOLD(x)   COLOR_BOOL ? _F_GREEN_BOLD(x) : x
#define F_YELLOW_BOLD(x)  COLOR_BOOL ? _F_YELLOW_BOLD(x) : x
#define F_RED_BOLD(x)     COLOR_BOOL ? _F_RED_BOLD(x) : x
#define F_CYAN_BOLD(x)    COLOR_BOOL ? _F_CYAN_BOLD(x) : x
#define F_BLUE_BOLD(x)    COLOR_BOOL ? _F_BLUE_BOLD(x) : x
#define F_MAGENTA_BOLD(x) COLOR_BOOL ? _F_MAGENTA_BOLD(x) : x
#define ANSI_DEFAULTS(x)  COLOR_BOOL ? _ANSI_DEFAULTS(x) : x

#define CLEAR_SCREEN "\033[2J"
#define GOTO_TOP "\033[0;0H"

#endif
