#pragma once

struct IntroSkipConfig
{
    bool logos = true;    // Eidos / TT logos ([Intro] SkipLogos)
    bool intro = true;    // story intro at startup ([Intro] SkipIntro)
    bool attract = true;  // LEGO Star Wars advert on the idle title menu ([Intro] SkipAttract)
};

bool InstallIntroSkip(const IntroSkipConfig& cfg);
