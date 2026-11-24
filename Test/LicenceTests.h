#pragma once
// Licence tests shared by every 99Hertz Pro plugin's test app (keep identical across plugins).
//
// Needs real keys for this product, made by the private signing tool and passed in the environment
// (never committed):  HZ_KEY_PRO (with an order number), HZ_KEY_PRO_NOORDER, HZ_KEY_ALL (bundle key),
// HZ_KEY_TRIAL (fresh 7-day), HZ_KEY_EXPIRED (ended trial); and HZ_TEST_SERVER, the base URL of an
// activation server built to plugin-activation-spec.md with test modes as path prefixes:
// <base>/ok/, /limit/ (always "in use on Studio iMac"), /slow/ (answers after 8 s), /othermid/ and
// /otherproduct/ (sign a token for the wrong computer / plugin), /html/ (an error page).
// Must run with CFFIXED_USER_HOME pointing at a scratch folder: the tests write and delete the key and
// activation files, and refuse to run if they'd be the real ones in ~/Library/Application Support/99Hertz/Licences.
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
    static const char* n[] = { "none", "trial", "pro", "trialExpired", "invalid", "saveFailed", "needsActivation" };
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

// Lets the editor's own timer run (most editors inherit Timer privately, so it can't be called directly).
inline void tickEditor (juce::AudioProcessorEditor& ed)
{
    if (auto* t = dynamic_cast<juce::Timer*> (&ed)) t->timerCallback();
    pump (150);
}

// Clicks the visible button showing `text` (runs its onClick). False if there isn't one.
inline bool clickButton (juce::Component& root, const juce::String& text)
{
    for (auto* ch : root.getChildren())
    {
        if (! ch->isVisible()) continue;
        if (auto* b = dynamic_cast<juce::Button*> (ch))
            if (b->getButtonText() == text && b->isEnabled() && b->onClick) { b->onClick(); return true; }
        if (clickButton (*ch, text)) return true;
    }
    return false;
}

// The visible text box whose placeholder is `placeholder` (the panel's key box is "99HZ-...").
inline juce::TextEditor* findTextBox (juce::Component& root, const juce::String& placeholder)
{
    for (auto* ch : root.getChildren())
    {
        if (! ch->isVisible()) continue;
        if (auto* t = dynamic_cast<juce::TextEditor*> (ch))
            if (t->getTextToShowWhenEmpty() == placeholder) return t;
        if (auto* t = findTextBox (*ch, placeholder)) return t;
    }
    return nullptr;
}

// The upgrade panel's current message (each panel publishes it as the "hzMessage" property).
inline juce::String panelMessage (juce::Component& root)
{
    for (auto* ch : root.getChildren())
    {
        if (ch->getProperties().contains ("hzMessage")) return ch->getProperties()["hzMessage"].toString();
        const auto m = panelMessage (*ch);
        if (m.isNotEmpty()) return m;
    }
    return {};
}

// Runs a licence request and waits (message loop running) for its callback.
struct Result { bool done = false; hz::LicenceInfo info; juce::String error; };
inline Result await (std::function<void (hz::Licence::Callback)> start, int maxMs = 20000)
{
    auto r = std::make_shared<Result>();
    start ([r] (hz::LicenceInfo i, juce::String e) { r->done = true; r->info = i; r->error = e; });
    for (int t = 0; t < maxMs && ! r->done; t += 20) pump (20);
    return *r;
}

// Pumps until no request is in flight and the panel has a final (non "...ING...") message.
inline void awaitPanel (juce::Component& root, int maxMs = 20000)
{
    for (int t = 0; t < maxMs; t += 20)
    {
        pump (20);
        if (hz::Licence::requestsInFlight() == 0 && ! panelMessage (root).endsWith ("...")) return;
    }
}

inline void setServer (const juce::String& mode)
{
    hz::Licence::setServerBaseForTests (mode == "offline" ? juce::String ("http://127.0.0.1:9/")
                                                          : env ("HZ_TEST_SERVER") + "/" + mode + "/");
}

// Fixes 1-5 (+ the shared button wording). Proc needs: licence, licenceInfo, setLicenceInfo, licenceWatcher.
// isUnlocked reads the processor's audio-thread flag.
template <typename Proc>
void runLicenceTests (const juce::String& product, std::function<bool (Proc&)> isUnlocked)
{
    std::printf ("\nLICENCE TESTS (%s)\n", product.toRawUTF8());
    const auto kPro = env ("HZ_KEY_PRO"), kTrial = env ("HZ_KEY_TRIAL"), kExpired = env ("HZ_KEY_EXPIRED");
    const auto kProNoOrder = env ("HZ_KEY_PRO_NOORDER"), kAll = env ("HZ_KEY_ALL");
  {
    hz::Licence lic (product);
    if (! keyFileIsScratch (lic))
    {
        std::printf ("  SKIPPED: set CFFIXED_USER_HOME to a scratch folder (key file would be %s)\n",
                     lic.keyFile().getFullPathName().toRawUTF8());
        ++failures; return;
    }
    if (kPro.isEmpty() || kTrial.isEmpty() || kExpired.isEmpty() || kProNoOrder.isEmpty() || kAll.isEmpty() || env ("HZ_TEST_SERVER").isEmpty())
    {
        std::printf ("  SKIPPED: HZ_KEY_PRO / _PRO_NOORDER / _ALL / _TRIAL / _EXPIRED / HZ_TEST_SERVER not set\n");
        ++failures; return;
    }
    const auto keyFile = lic.keyFile();
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    auto reset = [&]
    {
        keyFile.getParentDirectory().setReadOnly (false);
        keyFile.deleteFile(); lic.tokenFile().deleteFile(); lic.allTokenFile().deleteFile();
    };
    reset();
    setServer ("ok");
    juce::String goodToken;

    // ── A. Machine id ──
    {
        const auto mid = hz::Licence::machineId();
        std::printf ("  machine id %s  name \"%s\"\n", mid.toRawUTF8(), hz::Licence::machineName().toRawUTF8());
        expect (mid.length() == 32 && mid.containsOnly ("0123456789abcdef"), "machine id is 32 hex chars");
        expect (juce::SystemStats::getUniqueDeviceID().isNotEmpty(), "this computer has a unique device id (not the fallback)");
        expect (hz::Licence::machineId() == mid, "machine id is the same on every call");
        expect (hz::Licence::machineName().isNotEmpty() && hz::Licence::machineName().length() <= 64, "machine name is 1-64 chars");
    }

    // ── B. Pro key without a token: needsActivation, plays as free ──
    {
        reset();
        const auto a = lic.apply (kPro);
        expect (a.kind == hz::LicenceInfo::needsActivation && lic.check().kind == hz::LicenceInfo::needsActivation,
                juce::String ("pro key with no token -> ") + kindName (a.kind));
        expect (! a.unlocked(), "needsActivation isn't unlocked");
        expect (a.buttonText() == "ACTIVATE", "needsActivation button: " + a.buttonText());
        expect (a.order == 10423 && a.licensedText() == "LICENSED TO BUYER@EXAMPLE.COM" + dot + "ORDER #10423",
                "licensed text with order: " + a.licensedText());
        expect (a.ownerText() == "buyer@example.com" + dot + "ORDER #10423", "owner text: " + a.ownerText());
        const auto n = hz::Licence::verify (kProNoOrder, product);
        expect (n.order == 0 && n.licensedText() == "LICENSED TO BUYER@EXAMPLE.COM" && n.ownerText() == "buyer@example.com",
                "key without an order number: " + n.licensedText());
        expect (hz::Licence::verify (kPro, product).kind == hz::LicenceInfo::pro, "verify() alone still says pro (key is genuine)");
        reset();
    }

    // ── C. Activation: a valid token unlocks ──
    {
        reset();
        auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock ("Your key: " + kPro + ".", cb); });
        expect (r.done && r.error.isEmpty() && r.info.kind == hz::LicenceInfo::pro,
                juce::String ("unlock pro key online -> ") + kindName (r.info.kind) + " " + r.error);
        expect (lic.check().kind == hz::LicenceInfo::pro, "saved key + token check as pro");
        expect (keyFile.loadFileAsString() == hz::Licence::cleanKey (kPro), "the cleaned key is saved");
        expect (lic.tokenFile().existsAsFile() && ! lic.allTokenFile().existsAsFile(), "token saved as <product>.act");
        goodToken = lic.tokenFile().loadFileAsString();
        expect (goodToken.startsWith ("99HA-"), "token looks like 99HA-...");

        // the token is bound to this computer, this product and this key
        const auto mid = hz::Licence::machineId();
        expect (hz::Licence::tokenMatches (goodToken, kPro, product, mid), "token matches key/product/machine");
        expect (! hz::Licence::tokenMatches (goodToken, kPro, product, juce::String::repeatedString ("ab", 16)), "token for another machine is refused");
        expect (! hz::Licence::tokenMatches (goodToken, kPro, product + "x", mid), "token for another product is refused");
        expect (! hz::Licence::tokenMatches (goodToken, kProNoOrder, product, mid), "token for another key is refused");

        // tampered tokens
        const auto body = goodToken.upToFirstOccurrenceOf (".", false, false), sig = goodToken.fromFirstOccurrenceOf (".", false, false);
        auto flip = [] (juce::String t, int i) { const auto c = t[i]; return t.substring (0, i) + juce::String::charToString (c == 'A' ? 'B' : 'A') + t.substring (i + 1); };
        for (auto bad : { flip (body, 12) + "." + sig, body + "." + flip (sig, 10), body + ".", juce::String ("99HA-x.y"), goodToken.replace ("99HA-", "99HZ-") })
        {
            lic.tokenFile().replaceWithText (bad);
            expect (lic.check().kind == hz::LicenceInfo::needsActivation, "tampered token is refused -> " + juce::String (kindName (lic.check().kind)));
        }
        lic.tokenFile().replaceWithText (goodToken);
        expect (lic.check().kind == hz::LicenceInfo::pro, "restored token unlocks again");
        // token copied to another product's file doesn't unlock that product
        hz::Licence other (product + "zz");
        other.apply (kPro);   // invalid for that product anyway
        expect (other.check().kind == hz::LicenceInfo::none, "other product isn't unlocked by this key");
        // token file with junk around it (a stray newline) still works
        lic.tokenFile().replaceWithText (goodToken + "\n");
        expect (lic.check().kind == hz::LicenceInfo::pro, "token with a trailing newline still works");
        lic.tokenFile().replaceWithText (goodToken);
    }

    // ── D. Server tokens for the wrong computer / plugin are refused and nothing is saved ──
    {
        for (auto mode : { "othermid", "otherproduct", "html" })
        {
            reset();
            setServer (mode);
            auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
            expect (r.done && r.error.isNotEmpty() && lic.check().kind == hz::LicenceInfo::none && ! keyFile.existsAsFile()
                        && ! lic.tokenFile().existsAsFile(),
                    juce::String ("server mode ") + mode + ": refused, nothing saved (" + r.error + ")");
        }
        setServer ("ok");
    }

    // ── E. Offline and limit ──
    {
        reset();
        setServer ("offline");
        auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
        expect (r.done && r.error == "Connect to the internet once to activate", "offline: " + r.error);
        expect (lic.check().kind == hz::LicenceInfo::none && ! keyFile.existsAsFile(), "offline: key not saved, still locked");

        setServer ("limit");
        r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
        expect (r.done && r.error.contains ("Studio iMac") && r.error.contains ("another computer"), "limit: " + r.error);
        expect (lic.check().kind == hz::LicenceInfo::none, "limit: still locked");

        // a trial running here isn't lost when a pro activation fails
        lic.apply (kTrial);
        r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
        expect (r.error.isNotEmpty() && lic.check().kind == hz::LicenceInfo::trial && r.info.kind == hz::LicenceInfo::trial,
                "failed activation keeps the running trial");
        setServer ("ok");
        reset();
    }

    // ── F. Move to another computer ──
    {
        reset();
        auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
        expect (r.info.kind == hz::LicenceInfo::pro, "activated before moving");
        setServer ("offline");
        r = await ([&] (hz::Licence::Callback cb) { lic.deactivate (cb); });
        expect (r.error.isNotEmpty() && lic.check().kind == hz::LicenceInfo::pro && lic.tokenFile().existsAsFile(),
                "move while offline: nothing changes (" + r.error + ")");
        setServer ("ok");
        r = await ([&] (hz::Licence::Callback cb) { lic.deactivate (cb); });
        expect (r.error.isEmpty() && r.info.kind == hz::LicenceInfo::needsActivation, juce::String ("move -> ") + kindName (r.info.kind));
        expect (! lic.tokenFile().existsAsFile() && keyFile.existsAsFile(), "move deletes the token and keeps the key");
        r = await ([&] (hz::Licence::Callback cb) { lic.activate (cb); });
        expect (r.error.isEmpty() && r.info.kind == hz::LicenceInfo::pro && lic.check().kind == hz::LicenceInfo::pro, "activate the saved key again -> pro");
        goodToken = lic.tokenFile().loadFileAsString();
        reset();
        r = await ([&] (hz::Licence::Callback cb) { lic.activate (cb); });
        expect (r.error.isNotEmpty() && r.info.kind == hz::LicenceInfo::none, "activate with no key: " + r.error);
    }

    // ── G. Bundle key: all.act covers every product ──
    {
        reset();
        auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kAll, cb); });
        expect (r.info.kind == hz::LicenceInfo::pro && lic.allTokenFile().existsAsFile() && ! lic.tokenFile().existsAsFile(),
                juce::String ("bundle key -> ") + kindName (r.info.kind) + ", token in all.act");
        for (auto otherProduct : { "autochainpro", "autopresspro", "autobuildpro", "autodelaypro", "djtools", "autochop", "autoreverbpro", "autoclippro" })
        {
            if (product == otherProduct) continue;
            hz::Licence o (otherProduct);
            o.keyFile().deleteFile();
            const auto inFlight = hz::Licence::requestsInFlight();
            auto ro = await ([&] (hz::Licence::Callback cb) { o.unlock (kAll, cb); }, 100);
            expect (ro.done && ro.info.kind == hz::LicenceInfo::pro && o.check().kind == hz::LicenceInfo::pro && hz::Licence::requestsInFlight() == inFlight,
                    juce::String ("bundle key in ") + otherProduct + ": unlocked by all.act, no request");
            o.keyFile().deleteFile();
        }
        // ...but the bundle token doesn't unlock a different single-product key
        keyFile.replaceWithText (hz::Licence::cleanKey (kPro));
        expect (lic.check().kind == hz::LicenceInfo::needsActivation, "all.act doesn't cover another key");
        reset();
    }

    // ── J. Live 99hertz.com (only with HZ_LIVE=1): a test key that was never sold is refused as the spec says ──
    if (env ("HZ_LIVE") == "1")
    {
        reset();
        hz::Licence::setServerBaseForTests ("https://99hertz.com/api/plugin/");
        auto r = await ([&] (hz::Licence::Callback cb) { lic.unlock (kPro, cb); });
        expect (r.done && r.error == "This key has been turned off. Email info@99hertz.com." && ! keyFile.existsAsFile(),
                "live activate.php (unsold test key): " + r.error);
        keyFile.replaceWithText (hz::Licence::cleanKey (kPro));
        r = await ([&] (hz::Licence::Callback cb) { lic.deactivate (cb); });
        expect (r.done && r.error.isEmpty() && keyFile.existsAsFile(), "live deactivate.php answers ok (" + r.error + ")");
        setServer ("ok");
        reset();
    }

    // ── H. Panel: limit message, unlock, other instance follows, move, offline, activate ──
    {
        reset();
        Proc a, b;
        std::unique_ptr<juce::AudioProcessorEditor> edA (a.createEditor()), edB (b.createEditor());
        tickEditor (*edA); tickEditor (*edB);
        expect (clickButton (*edA, "UPGRADE"), "panel: UPGRADE opens the panel");
        auto* box = findTextBox (*edA, "99HZ-...");
        expect (box != nullptr, "panel: key box found");
        if (box != nullptr)
        {
            setServer ("limit");
            box->setText (kPro);
            expect (clickButton (*edA, "UNLOCK"), "panel: UNLOCK clicked");
            awaitPanel (*edA);
            tickEditor (*edA);
            expect (panelMessage (*edA).contains ("STUDIO IMAC") && ! isUnlocked (a), "panel shows the limit message: " + panelMessage (*edA));

            setServer ("offline");
            clickButton (*edA, "UNLOCK");
            awaitPanel (*edA);
            expect (panelMessage (*edA) == "CONNECT TO THE INTERNET ONCE TO ACTIVATE" && ! isUnlocked (a), "panel offline: " + panelMessage (*edA));

            setServer ("ok");
            clickButton (*edA, "UNLOCK");
            awaitPanel (*edA);
            tickEditor (*edA);
            expect (isUnlocked (a) && a.licenceInfo.kind == hz::LicenceInfo::pro, "panel: UNLOCK activates -> " + panelMessage (*edA));
            expect (hasButtonWithText (*edA, "LICENSED") && hasButtonWithText (*edA, "MOVE TO ANOTHER COMPUTER"), "panel: LICENSED + MOVE TO ANOTHER COMPUTER shown");
            expect (a.licenceInfo.licensedText() == "LICENSED TO BUYER@EXAMPLE.COM" + dot + "ORDER #10423", "panel: " + a.licenceInfo.licensedText());

            const auto t0 = juce::Time::getMillisecondCounter();
            while (! isUnlocked (b) && juce::Time::getMillisecondCounter() - t0 < 4000) pump (50);
            const auto took = juce::Time::getMillisecondCounter() - t0;
            tickEditor (*edB);
            expect (isUnlocked (b) && took <= 2300, "other instance follows the activation in " + juce::String (took) + " ms");
            expect (hasButtonWithText (*edB, "LICENSED"), "other instance's licence button reads LICENSED (" + b.licenceInfo.buttonText() + ")");

            clickButton (*edA, "MOVE TO ANOTHER COMPUTER");
            awaitPanel (*edA);
            tickEditor (*edA);
            expect (a.licenceInfo.kind == hz::LicenceInfo::needsActivation && ! isUnlocked (a), "panel: MOVE -> needsActivation (" + panelMessage (*edA) + ")");
            expect (hasButtonWithText (*edA, "ACTIVATE THIS COMPUTER"), "panel: ACTIVATE THIS COMPUTER shown after moving");
            const auto t1 = juce::Time::getMillisecondCounter();
            while (isUnlocked (b) && juce::Time::getMillisecondCounter() - t1 < 4000) pump (50);
            tickEditor (*edB);
            expect (! isUnlocked (b) && b.licenceInfo.kind == hz::LicenceInfo::needsActivation && hasButtonWithText (*edB, "ACTIVATE"),
                    "other instance follows the move in " + juce::String (juce::Time::getMillisecondCounter() - t1) + " ms");

            setServer ("offline");
            clickButton (*edA, "ACTIVATE THIS COMPUTER");
            awaitPanel (*edA);
            expect (panelMessage (*edA) == "CONNECT TO THE INTERNET ONCE TO ACTIVATE" && ! isUnlocked (a), "ACTIVATE offline: " + panelMessage (*edA));
            setServer ("ok");
            clickButton (*edA, "ACTIVATE THIS COMPUTER");
            awaitPanel (*edA);
            expect (isUnlocked (a) && a.licenceInfo.kind == hz::LicenceInfo::pro, "ACTIVATE THIS COMPUTER -> pro (" + panelMessage (*edA) + ")");
            goodToken = lic.tokenFile().loadFileAsString();
        }
        edA.reset(); edB.reset();
        reset();
    }

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
        expect (a.kind == hz::LicenceInfo::needsActivation, "order: pro over trial is saved (needs activating)");
        a = lic.apply (kTrial);
        expect (a.isProKey() && lic.check().isProKey(), "order: trial over a pro key is refused");
        a = lic.apply (kExpired);
        expect (a.isProKey() && lic.check().isProKey(), "order: ended trial over a pro key is refused");
        lic.tokenFile().replaceWithText (goodToken);
        expect (lic.check().kind == hz::LicenceInfo::pro, "order: with its token the pro key is pro");
        a = lic.apply (kTrial);
        expect (a.kind == hz::LicenceInfo::pro && lic.check().kind == hz::LicenceInfo::pro, "order: trial over activated pro is refused");
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
        expect (b.isProKey(), "same key saves once the folder is writable again");
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
        lic.tokenFile().replaceWithText (goodToken);
        hz::Licence (product).apply (kPro);          // an activation that finished with no panel left
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
        li.kind = hz::LicenceInfo::needsActivation;
        expect (li.buttonText() == "ACTIVATE", "button (needs activation): " + li.buttonText());
    }
    reset();
  }

    // ── I. Closing the plugin mid-request: no crash, no late callback, no thread left behind ──
    // (needs every Licence of this binary gone, so it runs after `lic` above is destroyed)
    {
        setServer ("slow");
        auto called = std::make_shared<std::atomic<bool>> (false);
        auto proc = std::make_unique<Proc>();
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        tickEditor (*ed);
        clickButton (*ed, "UPGRADE");
        if (auto* box = findTextBox (*ed, "99HZ-...")) box->setText (kPro);
        clickButton (*ed, "UNLOCK");
        { hz::Licence l2 (product); l2.requestTrial ("someone@example.com", false, [called] (hz::LicenceInfo, juce::String) { called->store (true); }); }
        pump (300);
        expect (hz::Licence::requestsInFlight() == 2, "two requests in flight: " + juce::String (hz::Licence::requestsInFlight()));
        const auto t0 = juce::Time::getMillisecondCounter();
        ed.reset(); proc.reset();   // the host closes the plugin
        const auto took = juce::Time::getMillisecondCounter() - t0;
        expect (took < 3000, "closing mid-request returns in " + juce::String (took) + " ms (requests cancelled)");
        expect (hz::Licence::requestsInFlight() == 0, "no request left running after close");
        pump (1500);
        expect (! called->load(), "no callback after the plugin closed");
        hz::Licence l3 (product);
        expect (! l3.keyFile().existsAsFile(), "nothing saved by the cancelled request");
        setServer ("ok");
    }
}

// Renders the activation states into outDir (licence-10..14), driving the real panel against HZ_TEST_SERVER.
template <typename Proc>
void renderLicenceStates (const juce::String& product, const juce::File& outDir)
{
    const auto kPro = env ("HZ_KEY_PRO");
    hz::Licence lic (product);
    if (! keyFileIsScratch (lic) || kPro.isEmpty() || env ("HZ_TEST_SERVER").isEmpty()) { std::printf ("  (licence renders skipped)\n"); return; }
    auto reset = [&] { lic.keyFile().deleteFile(); lic.tokenFile().deleteFile(); lic.allTokenFile().deleteFile(); };
    auto save = [&] (juce::AudioProcessorEditor& ed, const char* name)
    {
        tickEditor (ed); pump (200);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream fo (outDir.getChildFile (name));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
    };
    auto shoot = [&] (const char* name, std::function<void (Proc&, juce::AudioProcessorEditor&)> act)
    {
        Proc p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        tickEditor (*ed);
        act (p, *ed);
        save (*ed, name);
    };
    reset();
    lic.apply (kPro);   // a saved pro key, this computer not activated (what Sam's existing installs look like)
    shoot ("licence-10-needs-activation.png", [] (Proc&, juce::AudioProcessorEditor&) {});
    shoot ("licence-11-needs-activation-panel.png", [] (Proc&, juce::AudioProcessorEditor& ed) { clickButton (ed, "ACTIVATE"); });
    shoot ("licence-12-activate-offline-panel.png", [] (Proc&, juce::AudioProcessorEditor& ed)
    {
        clickButton (ed, "ACTIVATE"); setServer ("offline");
        clickButton (ed, "ACTIVATE THIS COMPUTER"); awaitPanel (ed);
    });
    reset();
    shoot ("licence-13-key-in-use-panel.png", [&] (Proc&, juce::AudioProcessorEditor& ed)
    {
        clickButton (ed, "UPGRADE"); setServer ("limit");
        if (auto* box = findTextBox (ed, "99HZ-...")) box->setText (kPro);
        clickButton (ed, "UNLOCK"); awaitPanel (ed);
    });
    shoot ("licence-14-activated-panel.png", [&] (Proc&, juce::AudioProcessorEditor& ed)
    {
        clickButton (ed, "UPGRADE"); setServer ("ok");
        if (auto* box = findTextBox (ed, "99HZ-...")) box->setText (kPro);
        clickButton (ed, "UNLOCK"); awaitPanel (ed);
    });
    shoot ("licence-15-licensed-panel.png", [] (Proc&, juce::AudioProcessorEditor& ed) { clickButton (ed, "LICENSED"); });
    reset();
    setServer ("ok");
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
