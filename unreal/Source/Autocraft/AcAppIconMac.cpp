// The game's Dock icon on the Mac. The game runs as `UnrealEditor -game`,
// whose bundle carries the editor's icon, so a game run swaps in the
// Autocraft one (Build/Mac/Resources/Assets.xcassets, which a packaged build
// takes as its app icon). The editor itself keeps its own icon.
#include "AcAppIcon.h"

#include "Misc/Paths.h"

#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"  // Cocoa, with UE's FVector clash worked around
#endif

void AcAppIcon::Apply()
{
#if PLATFORM_MAC
	if (GIsEditor) return;
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
		/ TEXT("Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_512x512@2x.png"));
	if (!FPaths::FileExists(Path)) return;
	NSString* File = [NSString stringWithUTF8String:TCHAR_TO_UTF8(*Path)];
	// AppKit wants the main thread; the game thread is not it on the Mac.
	dispatch_async(dispatch_get_main_queue(), ^{
		if (NSImage* Image = [[NSImage alloc] initWithContentsOfFile:File])
		{
			[NSApp setApplicationIconImage:Image];
			[Image release];
		}
	});
#endif
}
