#include "AcSettings.h"

#include "Engine/Engine.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"

UAcSettings& UAcSettings::Get()
{
	if (GEngine)
	{
		if (UAcSettings* S = Cast<UAcSettings>(GEngine->GetGameUserSettings())) return *S;
	}
	// The ini does not name this class (or no engine yet): our own copy.
	static TStrongObjectPtr<UAcSettings> Own;
	if (!Own)
	{
		Own.Reset(NewObject<UAcSettings>(GetTransientPackage(), TEXT("AcSettings")));
		Own->LoadSettings(false);
	}
	return *Own;
}

void UAcSettings::Store()
{
	Get().SaveSettings();
}

bool UAcSettings::Remembers()
{
	return !FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("AcRemember"));
}
