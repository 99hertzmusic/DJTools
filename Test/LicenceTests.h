#pragma once
// Licence tests shared by every 99Hertz Pro plugin's test app (keep identical across plugins).
//
// Needs real keys for this product, made by the private signing tool and passed in the environment
// (never committed):  HZ_KEY_PRO, HZ_KEY_TRIAL (fresh 7-day), HZ_KEY_EXPIRED (ended trial).
// Must run with CFFIXED_USER_HOME pointing at a scratch folder: the tests write and delete the key file,
// and refuse to run if it would be the real one in ~/Library/Application Support/99Hertz/Licences.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace hztest
{
inline int failures = 0;

inline void expect (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

inline juce::String env (const char* name)
{
    const char* v = std::getenv (name);
    return v != nullptr ? juce::String (v) : juce::String();
}

inline const char* kindName (hz::LicenceInfo::Kind k)
{
    static const char* n[] = { "none", "trial", "pro", "trialExpired", "invalid", "saveFailed" };
    return n[(int) k];
}

// Runs the message loop for a while (timers fire), or just waits if modal loops aren't compiled in.
inline void pump (int ms)
{
   #if JUCE_MODAL_LOOPS_PERMITTED
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
   #else
    juce::Thread::sleep (ms);
   #endif
}

// The safety check: is the key file inside the scratch home?
inline bool keyFileIsScratch (const hz::Licence& l)
{
    const auto home = env ("CFFIXED_USER_HOME");
    const auto path = l.keyFile().getFullPathName();
    // (macOS may report /private/tmp/... as /tmp/..., so compare the folder name; never the real $HOME)
    const auto realHome = env ("HOME");
    return home.isNotEmpty() && realHome.isNotEmpty() && path.contains (juce::File (home).getFileName()) && ! path.startsWith (realHome);
}

// Finds a visible button showing exactly `text` in a component tree (hidden panels are skipped).
inline bool hasButtonWithText (juce::Component& root, const juce::String& text)
{
    for (auto* ch : root.getChildren())
    {
        if (! ch->isVisible()) continue;
        if (auto* b = dynamic_cast<juce::Button*> (ch))
            if (b->getButtonText() == text) return true;
        if (hasButtonWithText (*ch, text)) return true;
    }
    return false;
}

inline void tickEditor (juce::AudioProcessorEditor& ed)
{
    if (auto* t = dynamic_cast<juce::Timer*> (&ed)) t->timerCallback();
}

// Fixes 1-5 (+ the shared button wording). Proc needs: licence, licenceInfo, setLicenceInfo, licenceWatcher.
// isUnlocked reads the processor's audio-thread flag.
template <typename Proc>
void runLicenceTests (const juce::String& product, std::function<bool (Proc&)> isUnlocked)
{
    std::printf ("\nLICENCE TESTS (%s)\n", product.toRawUTF8());
    const auto kPro = env ("HZ_KEY_PRO"), kTrial = env ("HZ_KEY_TRIAL"), kExpired = env ("HZ_KEY_EXPIRED");
    hz::Licence lic (product);
    if (! keyFileIsScratch (lic))
    {
        std::printf ("  SKIPPED: set CFFIXED_USER_HOME to a scratch folder (key file would be %s)\n",
                     lic.keyFile().getFullPathName().toRawUTF8());
        ++failures; return;
    }
    if (kPro.isEmpty() || kTrial.isEmpty() || kExpired.isEmpty())
    {
        std::printf ("  SKIPPED: HZ_KEY_PRO / HZ_KEY_TRIAL / HZ_KEY_EXPIRED not set\n");
        ++failures; return;
    }
    const auto keyFile = lic.keyFile();
    auto reset = [&] { keyFile.getParentDirectory().setReadOnly (false); keyFile.deleteFile(); };
    reset();

    // ── 5. daysLeft ──
    {
        const auto now = juce::Time::currentTimeMillis() / 1000;
        hz::LicenceInfo li; li.kind = hz::LicenceInfo::trial;
        li.expires = now + 7 * 86400;          expect (li.daysLeft() == 7, "daysLeft: fresh 7-day trial shows 7 (was 8)");
        li.expires = now + 7 * 86400 - 3600;   expect (li.daysLeft() == 7, "daysLeft: 7 days - 1 h shows 7");
        li.expires = now + 6 * 86400 + 3600;   expect (li.daysLeft() == 7, "daysLeft: 6 days + 1 h shows 7");
        li.expires = now + 86400 - 600;        expect (li.daysLeft() == 1, "daysLeft: last day shows 1");
        li.expires = now + 120;                expect (li.daysLeft() == 0, "daysLeft: inside the 5 min grace shows 0");
        li.expires = now - 10;                 expect (li.daysLeft() == 0, "daysLeft: ended shows 0");
        const auto t = hz::Licence::verify (kTrial, product);
        expect (t.kind == hz::LicenceInfo::trial && t.daysLeft() == 7, "daysLeft: real fresh trial key -> " + juce::String (t.daysLeft()));
    }

    // ── 3. Paste cleanup ──
    {
        const juce::String nbsp (juce::CharPointer_UTF8 ("\xc2\xa0")), zw (juce::CharPointer_UTF8 ("\xe2\x80\x8b"));
        const auto half = kPro.length() / 2;
        const juce::String pastes[] = {
            kPro,
            "Key: " + kPro,
            "Your key: " + kPro + ".",
            "  " + kPro + "\n",
            nbsp + kPro + nbsp,
            kPro.substring (0, half) + zw + kPro.substring (half),
            kPro.substring (0, half) + " \n" + kPro.substring (half),
            "\"" + kPro + "\"",
            juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + kPro + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d.")),
        };
        const char* names[] = { "plain", "\"Key: ...\"", "\"Your key: ....\" (full stop)", "spaces + newline", "non-breaking spaces",
                                "zero-width space inside", "line break inside", "straight quotes", "curly quotes + full stop" };
        int i = 0;
        for (auto& p : pastes)
        {
            const auto info = hz::Licence::verify (p, product);
            expect (info.kind == hz::LicenceInfo::pro, juce::String ("paste: ") + names[i++] + " -> " + kindName (info.kind));
        }
        expect (hz::Licence::verify ("hello", product).kind == hz::LicenceInfo::invalid, "paste: junk is invalid");
        expect (hz::Licence::verify ("", product).kind == hz::LicenceInfo::none, "paste: empty is none");
        expect (hz::Licence::verify (kPro.dropLastCharacters (3), product).kind == hz::LicenceInfo::invalid, "paste: cut-off key is invalid");
        expect (hz::Licence::verify (kPro, product + "x").kind == hz::LicenceInfo::invalid, "paste: other product's key is invalid");
        // the saved file holds the clean key
        reset();
        lic.apply ("Key: " + kPro + ".");
        expect (keyFile.loadFileAsString() == hz::Licence::cleanKey (kPro), "apply saves the cleaned key");
        reset();
    }

    // ── 4. Never replace an unlocked key with a locked one ──
    {
        reset();
        auto a = lic.apply (kTrial);
        expect (a.kind == hz::LicenceInfo::trial, juce::String ("order: trial on empty -> ") + kindName (a.kind));
        a = lic.apply (kExpired);
        expect (a.kind == hz::LicenceInfo::trial && lic.check().kind == hz::LicenceInfo::trial,
                juce::String ("order: ended trial over running trial is refused -> ") + kindName (a.kind) + " / saved " + kindName (lic.check().kind));
        a = lic.apply (kPro);
        expect (a.kind == hz::LicenceInfo::pro && lic.check().kind == hz::LicenceInfo::pro, "order: pro over trial is saved");
        a = lic.apply (kTrial);
        expect (a.kind == hz::LicenceInfo::pro && lic.check().kind == hz::LicenceInfo::pro, "order: trial over pro is refused");
        a = lic.apply (kExpired);
        expect (a.kind == hz::LicenceInfo::pro && lic.check().kind == hz::LicenceInfo::pro, "order: ended trial over pro is refused");
        reset();
        a = lic.apply (kExpired);
        expect (a.kind == hz::LicenceInfo::trialExpired, "order: ended trial on empty is reported as ended");
        a = lic.apply (kTrial);
        expect (a.kind == hz::LicenceInfo::trial && lic.check().kind == hz::LicenceInfo::trial, "order: trial over ended trial is saved");
        reset();
    }

    // ── 2. Write failures ──
    {
        reset();
        auto dir = keyFile.getParentDirectory();
        dir.createDirectory();
        dir.setReadOnly (true);
        const auto a = lic.apply (kPro);
        expect (a.kind == hz::LicenceInfo::saveFailed, juce::String ("read-only licence folder -> ") + kindName (a.kind));
        expect (! a.unlocked(), "read-only licence folder doesn't unlock");
        dir.setReadOnly (false);
        const auto b = lic.apply (kPro);
        expect (b.kind == hz::LicenceInfo::pro, "same key saves once the folder is writable again");
        reset();
        // folder path blocked by a file
        auto parent = dir.getParentDirectory();
        dir.deleteRecursively();
        dir.replaceWithText ("not a folder");
        const auto c = lic.apply (kPro);
        expect (c.kind == hz::LicenceInfo::saveFailed, juce::String ("licence folder is a file -> ") + kindName (c.kind));
        dir.deleteFile();
        juce::ignoreUnused (parent);
    }

    // ── 1. Other open instances follow an unlock / removal ──
    {
        reset();
        Proc a, b;
        std::unique_ptr<juce::AudioProcessorEditor> edB (b.createEditor());
        tickEditor (*edB);
        expect (! isUnlocked (a) && ! isUnlocked (b), "two instances start locked");
        expect (hasButtonWithText (*edB, "UPGRADE"), "locked: licence button reads UPGRADE");

        const auto t = a.licence.apply (kTrial);   // what A's panel (or a trial request) does
        a.setLicenceInfo (t);
        pump (2600);
       #if ! JUCE_MODAL_LOOPS_PERMITTED
        b.licenceWatcher->poll();
       #endif
        tickEditor (*edB);
        expect (isUnlocked (b), "other instance unlocks within ~2 s of a key being saved");
        expect (hasButtonWithText (*edB, b.licenceInfo.buttonText()) && b.licenceInfo.buttonText().startsWith ("TRIAL"),
                "other instance's editor shows " + b.licenceInfo.buttonText());

        // a trial request that finishes after its panel closed: the key is saved by the worker, the processor follows
        reset();
        pump (2600);
       #if ! JUCE_MODAL_LOOPS_PERMITTED
        b.licenceWatcher->poll();
       #endif
        expect (! isUnlocked (b), "key file removed: instance locks again");
        hz::Licence (product).apply (kPro);          // the trial worker thread's apply(), with no panel left
        pump (2600);
       #if ! JUCE_MODAL_LOOPS_PERMITTED
        b.licenceWatcher->poll();
       #endif
        tickEditor (*edB);
        expect (b.licenceInfo.kind == hz::LicenceInfo::pro && isUnlocked (b), "key saved with no panel open is picked up");
        expect (hasButtonWithText (*edB, "LICENSED"), "licensed: licence button reads LICENSED");

        // unchanged file: a forced re-check doesn't fire again
        const int before = b.licenceVersion.load();
        b.licenceWatcher->poll (true);
        expect (b.licenceVersion.load() == before, "unchanged licence doesn't re-notify");
        edB.reset();
        reset();
    }

    // ── 6. Button wording ──
    {
        const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        hz::LicenceInfo li;
        expect (li.buttonText() == "UPGRADE", "button (no key): " + li.buttonText());
        li.kind = hz::LicenceInfo::trial; li.expires = juce::Time::currentTimeMillis() / 1000 + 3 * 86400 - 60;
        expect (li.buttonText() == "TRIAL" + dot + "3 DAYS LEFT" + dot + "UPGRADE", "button (trial): " + li.buttonText());
        li.kind = hz::LicenceInfo::trialExpired;
        expect (li.buttonText() == "TRIAL ENDED" + dot + "UPGRADE", "button (ended): " + li.buttonText());
        li.kind = hz::LicenceInfo::pro;
        expect (li.buttonText() == "LICENSED", "button (pro): " + li.buttonText());
    }
    reset();
}

// Fix 8 (Lite plugins): upgrade-only settings saved in a project survive opening it without a licence.
// setUpgrade puts upgrade-only values into the processor's parameters.
template <typename Proc>
void runSavedSettingsTests (std::function<void (Proc&)> setUpgrade)
{
    std::printf ("\nSAVED UPGRADE SETTINGS\n");
    auto params = [] (Proc& p)
    {
        juce::StringArray s;
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                s.add (r->getParameterID() + "=" + juce::String (r->getValue(), 4));
        return s.joinIntoString (" ");
    };

    juce::MemoryBlock state;
    juce::String saved;
    {
        Proc p;
        hz::LicenceInfo li; li.kind = hz::LicenceInfo::pro; p.setLicenceInfo (li);
        setUpgrade (p);
        expect (p.usesUpgradeSettings(), "pro session with upgrade settings is detected");
        p.getStateInformation (state);
        saved = params (p);
    }
    {
        Proc p;
        p.setLicenceInfo ({});
        p.setStateInformation (state.getData(), (int) state.getSize());
        expect (p.usesUpgradeSettings(), "opened without a licence: upgrade settings still in the session");
        expect (params (p) == saved, "opened without a licence: every saved value kept");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        tickEditor (*ed);
        juce::MemoryBlock again; p.getStateInformation (again);
        Proc q; q.setLicenceInfo ({}); q.setStateInformation (again.getData(), (int) again.getSize());
        expect (params (q) == saved, "saved again without a licence: values still kept");

        hz::LicenceInfo li; li.kind = hz::LicenceInfo::pro; q.setLicenceInfo (li);
        expect (params (q) == saved && q.usesUpgradeSettings(), "after upgrading: the settings are back");
    }
    {
        Proc p;
        hz::LicenceInfo li; li.kind = hz::LicenceInfo::pro; p.setLicenceInfo (li);
        expect (! p.usesUpgradeSettings(), "default settings aren't flagged");
    }
}

inline int finish()
{
    std::printf ("\n%s: %d licence test failure(s)\n", failures == 0 ? "ALL LICENCE TESTS PASSED" : "LICENCE TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
} // namespace hztest
