/*
 * Stoatworks Labs - About window data for Zoetrope.
 *
 * A PROVISIONAL HAND COPY, adapted from schlieren's generated header on
 * 2026-10-15, as graticule's was before it was registered. Zoetrope is not in
 * the website's projects.json yet, so stoatworks-backend/scripts/sync-about.py
 * has nothing to generate it from. Once it is registered this file is
 * generated and must not be edited here.
 *
 * `guide` is empty because no user guide exists yet, so the About block has no
 * guide button (StoatworksAboutLinks.h leaves out a link that is not there).
 * The page and repo links point where the project will live; neither exists
 * on 2026-10-15.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Zoetrope";
    inline constexpr auto slug = "zoetrope";
    inline constexpr auto hook = "A strobe-lit spinning disc of the clip's frames, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/zoetrope/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/zoetrope";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
