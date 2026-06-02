#pragma once

#ifdef __cplusplus

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#endif


//Not being used currently, but maybe in the future, so I am keeping this.
namespace colors {
#ifdef __OBJC__
    inline NSColor* make(float r, float g, float b, float a = 255.0f) {
        return [NSColor colorWithCalibratedRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:a/255.0];
    }

    inline NSColor* bg()      { return make(12, 12, 12); }
    inline NSColor* txt()     { return make(204, 204, 204); }
    inline NSColor* prompt()  { return make(78, 201, 176); }
    inline NSColor* err()     { return make(244, 71, 71); }
    inline NSColor* warn()    { return make(229, 192, 123); }
    inline NSColor* success() { return make(98, 198, 98); }
    inline NSColor* echo()    { return make(86, 156, 214); }
    inline NSColor* input()   { return make(255, 255, 255); }
    inline NSColor* dir()     { return make(78, 201, 176); }
    inline NSColor* exec()    { return make(98, 198, 98); }
    inline NSColor* file()    { return make(204, 204, 204); }
    inline NSColor* hidden()  { return make(100, 100, 100); }
#endif
}


#endif

namespace COLOR {
    // Control Sequences
    const char* const RESET       = "\033[0m";
    const char* const BOLD        = "\033[1m";
    const char* const DIM         = "\033[2m";
    const char* const UNDERLINE   = "\033[4m";

    // --- TrueColor (RGB) Palette ---
    // Modern 24-bit color palette for crisp, modern terminal interfaces
    namespace RGB {
        const char* const SUCCESS   = "\033[38;2;46;204;113m";  // Emerald Green
        const char* const WARN      = "\033[38;2;241;196;15m";   // Sun Yellow
        const char* const ERROR     = "\033[38;2;231;76;60m";    // Alizarin Red
        const char* const INFO      = "\033[38;2;52;152;219m";   // Peter River Blue
        const char* const HINT      = "\033[38;2;155;89;182m";   // Amethyst Purple

        // Grayscale / UI Elements
        const char* const WHITE     = "\033[38;2;236;240;241m";  // Clouds White
        const char* const LIGHT_GREY= "\033[38;2;189;195;199m";  // Silver
        const char* const GREY       = "\033[38;2;127;140;141m";  // Asbestos Grey
        const char* const DARK_GREY = "\033[38;2;44;62;80m";     // Midnight Blue
        const char* const BLACK     = "\033[38;2;21;21;21m";      // Deep Black
    }

    // --- Standard ANSI Palette (Fallback) ---
    // Legacy 8/16 color codes for maximum terminal compatibility
    namespace ANSI {
        const char* const SUCCESS   = "\033[32m";      // Green
        const char* const WARN      = "\033[33m";      // Yellow
        const char* const ERROR     = "\033[31m";      // Red
        const char* const INFO      = "\033[36m";      // Cyan
        const char* const HINT      = "\033[35m";      // Magenta

        const char* const WHITE     = "\033[37m";      // Bright White
        const char* const GREY      = "\033[90m";      // Bright Black (Dark Grey)
        const char* const BLACK     = "\033[30m";      // Black
    }
}