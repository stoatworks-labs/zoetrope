#include "Zoetrope.h"

/**
    The one registration.

    This file is listed directly in the Zoetrope MODULE target, not in
    zoetrope_core: `CFFGLPluginInfo` registers itself from a file-scope
    constructor and nothing ever references it by name, so in a STATIC
    archive the linker is entitled to drop the whole translation unit --
    giving a bundle that loads, exports `plugMain`, and reports that it
    contains no plugins. The core stays an OBJECT library for the same reason.

    The name is `SW Zoetrope`, eleven characters. The FFGL name field is
    `char[ 16 ]` and is **not** null-terminated, so the host truncates without
    saying so. `oxbow probe` is what reads it back the way a host does.
*/
namespace
{
class ZoetropeEffect : public zoetrope::ZoetropePlugin
{
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ZoetropeEffect >,// Create method
	"ZT01",                         // Plugin unique ID of maximum length 4
	"SW Zoetrope",                  // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_EFFECT,                      // Plugin type
	"The clip's frames printed on the cards of a spinning disc, seen only in the light of a strobe that fires on the "
	"music.\n\nEach card is printed with the clip as it passes a print head, the disc turns at a speed its drive gives "
	"it, and the camera sees it only in the flashes and a little room light. Turn exactly one card per flash and the "
	"figures stand still and each plays the clip, later round the ring; a little off and they crawl; a long flash "
	"smears them.",
	"Zoetrope FFGL effect"// About
);

extern "C" const char* ZoetropeBuildStamp()
{
	return "zoetrope " ZOETROPE_VERSION ", built " __DATE__ " " __TIME__;
}
